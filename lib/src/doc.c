#define _POSIX_C_SOURCE 200809L
#include "doc.h"
#include "value.h"
#include "arena.h"
#include "ast.h"
// claude + me = <3
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

typedef struct ConstEntry {
    const char        *name;
    AstNode           *constDecl; // NODE_DIRECTIVE_CONST
    struct ConstEntry *next;
} ConstEntry;

typedef struct EnumEntry {
    const char       *name;
    AstNode          *enumDecl;  // NODE_DIRECTIVE_ENUM 
    struct EnumEntry *next;
} EnumEntry;

typedef struct {
    ConstEntry *consts;
    EnumEntry  *enums;
} DocScope;

static AstNode *scopeLookupConst(const DocScope *s, const char *name) {
    for (ConstEntry *e = s->consts; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e->constDecl;
    }
    return NULL;
}

static AstNode *scopeLookupEnum(const DocScope *s, const char *name) {
    for (EnumEntry *e = s->enums; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e->enumDecl;
    }
    return NULL;
}

static void scopeFree(DocScope *s) {
    for (ConstEntry *e = s->consts, *n; e; e = n) { n = e->next; free(e); }
    for (EnumEntry  *e = s->enums,  *n; e; e = n) { n = e->next; free(e); }
    s->consts = NULL;
    s->enums  = NULL;
}

/* standard RFC 4648 alphabet; '=' padding maps to 0 (handled via srcLen) */
static const int8_t B64[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

/*
 * decode base64 text of length srcLen into arena-allocated buffer.
 * sets *outLen to the decoded byte count.
 * returns NULL on invalid input or allocation failure.
 */
static uint8_t *b64Decode(Arena *arena, const char *src, size_t srcLen,
                           size_t *outLen) {
    if (srcLen == 0) {
        *outLen = 0;
        return arenaAlloc(arena, 1);
    }

    /* count real (non-padding) chars */
    size_t realLen = srcLen;
    while (realLen > 0 && src[realLen - 1] == '=') realLen--;

    size_t decoded = (realLen * 3) / 4;
    uint8_t *out = arenaAlloc(arena, decoded + 1);
    if (!out) return NULL;

    size_t i = 0, o = 0;
    while (i + 3 < srcLen) {
        int8_t a = B64[(uint8_t)src[i]];
        int8_t b = B64[(uint8_t)src[i+1]];
        int8_t c = B64[(uint8_t)src[i+2]];
        int8_t d = B64[(uint8_t)src[i+3]];

        if (a < 0 || b < 0) return NULL;
        out[o++] = (uint8_t)((a << 2) | (b >> 4));
        if (src[i+2] != '=') {
            if (c < 0) return NULL;
            out[o++] = (uint8_t)((b << 4) | (c >> 2));
        }
        if (src[i+3] != '=') {
            if (d < 0) return NULL;
            out[o++] = (uint8_t)((c << 6) | d);
        }
        i += 4;
    }

    *outLen = o;
    return out;
}

/* ---- forward declaration ---- */

static scl_value_t *valueFromAst(AstNode *node, AstNode *typeNode,
                                  const DocScope *scope,
                                  Arena *arena, char **errOut);

/* ---- helpers ---- */

static AstNode *unwrapOptional(AstNode *t) {
    while (t && t->type == NODE_TYPE_OPTIONAL) t = t->as.optional.inner;
    return t;
}

static scl_value_t *valueFromKVList(AstKV *pairs, scl_type_t type,
                                     AstNode *elemTypeNode,
                                     const DocScope *scope,
                                     Arena *arena, char **errOut) {
    scl_value_t *v = valueAlloc(arena, type);
    for (AstKV *kv = pairs; kv; kv = kv->next) {
        scl_value_t *child = valueFromAst(kv->value, elemTypeNode,
                                           scope, arena, errOut);
        if (!child) return NULL;
        const char *key = arenaStrdup(arena, kv->key);
        valueKVSet(v, key, child, arena);
    }
    return v;
}

static bool enumHasVariant(AstNode *enumDecl, const char *name) {
    for (AstEnumVariant *v = enumDecl->as.enumDef.variants; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return true;
    }
    return false;
}

/* ---- core value builder ---- */

static scl_value_t *valueFromAst(AstNode *node, AstNode *typeNode,
                                  const DocScope *scope,
                                  Arena *arena, char **errOut) {
    switch (node->type) {
        case NODE_VAL_NULL:
            return valueAlloc(arena, SCL_TYPE_NULL);

        case NODE_VAL_BOOL: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_BOOL);
            v->as.b = node->as.boolVal.v;
            return v;
        }

        case NODE_VAL_INT: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_INT);
            v->as.i = node->as.intVal.v;
            return v;
        }

        case NODE_VAL_UINT: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_UINT);
            v->as.u = node->as.uintVal.v;
            return v;
        }

        case NODE_VAL_FLOAT: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_FLOAT);
            v->as.f = node->as.floatVal.v;
            return v;
        }

        case NODE_VAL_STRING: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_STRING);
            v->as.str.ptr = arenaStrndup(arena, node->as.strVal.str,
                                          node->as.strVal.len);
            v->as.str.len = node->as.strVal.len;
            return v;
        }

        case NODE_VAL_BYTES: {
            /* decode base64 text into raw binary bytes */
            size_t decodedLen = 0;
            uint8_t *decoded = b64Decode(arena,
                                          node->as.strVal.str,
                                          node->as.strVal.len,
                                          &decodedLen);
            if (!decoded) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "invalid base64 in bytes literal at line %d",
                         node->line);
                *errOut = strdup(buf);
                return NULL;
            }
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_BYTES);
            v->as.bytes.ptr = decoded;
            v->as.bytes.len = decodedLen;
            return v;
        }

        case NODE_VAL_DATE: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_DATE);
            v->as.str.ptr = arenaStrndup(arena, node->as.strVal.str,
                                          node->as.strVal.len);
            v->as.str.len = node->as.strVal.len;
            return v;
        }

        case NODE_VAL_DATETIME: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_DATETIME);
            v->as.str.ptr = arenaStrndup(arena, node->as.strVal.str,
                                          node->as.strVal.len);
            v->as.str.len = node->as.strVal.len;
            return v;
        }

        case NODE_VAL_DURATION: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_DURATION);
            v->as.str.ptr = arenaStrndup(arena, node->as.strVal.str,
                                          node->as.strVal.len);
            v->as.str.len = node->as.strVal.len;
            return v;
        }

        case NODE_VAL_LIST: {
            scl_value_t *v = valueAlloc(arena, SCL_TYPE_LIST);
            AstNode *elemType = NULL;
            AstNode *inner = unwrapOptional(typeNode);
            if (inner && inner->type == NODE_TYPE_LIST)
                elemType = inner->as.list.elem;
            for (size_t i = 0; i < node->as.listVal.count; i++) {
                scl_value_t *item = valueFromAst(node->as.listVal.items[i],
                                                  elemType, scope,
                                                  arena, errOut);
                if (!item) return NULL;
                valueListPush(v, item, arena);
            }
            return v;
        }

        case NODE_VAL_STRUCT: {
            AstNode *inner = unwrapOptional(typeNode);
            if (inner && inner->type == NODE_TYPE_MAP) {
                return valueFromKVList(node->as.kvVal.pairs, SCL_TYPE_MAP,
                                       inner->as.map.value, scope,
                                       arena, errOut);
            }
            return valueFromKVList(node->as.kvVal.pairs, SCL_TYPE_STRUCT,
                                   NULL, scope, arena, errOut);
        }

        /* NODE_VAL_MAP is never emitted by the parser but handle defensively */
        case NODE_VAL_MAP:
            return valueFromKVList(node->as.kvVal.pairs, SCL_TYPE_MAP,
                                   NULL, scope, arena, errOut);

        /*
         * $name either an @enum variant or a @const reference.
         *
         * enum variant: typeNode unwraps to NODE_TYPE_REF whose name matches
         *   an enum in scope and the variant name is valid.
         *   stored as SCL_TYPE_STRING containing the variant name.
         *
         * const reference: look up in scope and recurse on the const's value.
         */
        case NODE_VAL_REF: {
            const char *refName = node->as.valRef.name;

            /* check enum variant first */
            AstNode *inner = unwrapOptional(typeNode);
            if (inner && inner->type == NODE_TYPE_REF) {
                AstNode *enumDecl = scopeLookupEnum(scope, inner->as.typeRef.name);
                if (enumDecl && enumHasVariant(enumDecl, refName)) {
                    scl_value_t *v = valueAlloc(arena, SCL_TYPE_STRING);
                    v->as.str.ptr = arenaStrdup(arena, refName);
                    v->as.str.len = strlen(refName);
                    return v;
                }
            }

            /* const reference */
            AstNode *constDecl = scopeLookupConst(scope, refName);
            if (!constDecl) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "undefined reference '$%s' at line %d",
                         refName, node->line);
                *errOut = strdup(buf);
                return NULL;
            }
            return valueFromAst(constDecl->as.constDef.value,
                                 constDecl->as.constDef.typeNode,
                                 scope, arena, errOut);
        }

        /*
         * $base & { overrides... }
         *
         * resolve base const to a struct, then apply overrides on top.
         * right side wins on conflicting keys.
         */
        case NODE_VAL_MERGE: {
            const char *baseName = node->as.merge.baseName;
            AstNode *constDecl = scopeLookupConst(scope, baseName);
            if (!constDecl) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "undefined base '$%s' in merge at line %d",
                         baseName, node->line);
                *errOut = strdup(buf);
                return NULL;
            }

            scl_value_t *base = valueFromAst(constDecl->as.constDef.value,
                                              constDecl->as.constDef.typeNode,
                                              scope, arena, errOut);
            if (!base) return NULL;

            if (base->type != SCL_TYPE_STRUCT) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "merge base '$%s' must be a struct at line %d",
                         baseName, node->line);
                *errOut = strdup(buf);
                return NULL;
            }

            /* copy all base fields into the result struct */
            scl_value_t *result = valueAlloc(arena, SCL_TYPE_STRUCT);
            for (SclKV *kv = base->as.kv.head; kv; kv = kv->next) {
                const char *k = arenaStrdup(arena, kv->key);
                valueKVSet(result, k, kv->val, arena);
            }

            /* apply overrides — right side wins */
            for (AstKV *kv = node->as.merge.overrides; kv; kv = kv->next) {
                scl_value_t *val = valueFromAst(kv->value, NULL,
                                                 scope, arena, errOut);
                if (!val) return NULL;
                const char *k = arenaStrdup(arena, kv->key);
                valueKVSet(result, k, val, arena);
            }

            return result;
        }

        default: {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "unexpected AST node type %d at line %d",
                     node->type, node->line);
            *errOut = strdup(buf);
            return NULL;
        }
    }
}

// pub

scl_doc_t *docFromAst(Arena *astArena, AstNode *astDoc,
                      const char *src, char **errOut) {
    (void)astArena;
    (void)src;

    /* build const/enum scope from document-level directives */
    DocScope scope = {NULL, NULL};

    for (size_t i = 0; i < astDoc->as.document.count; i++) {
        AstNode *decl = astDoc->as.document.decls[i];
        if (!decl) continue;

        if (decl->type == NODE_DIRECTIVE_CONST) {
            ConstEntry *e = malloc(sizeof(ConstEntry));
            if (!e) { *errOut = strdup("out of memory"); scopeFree(&scope); return NULL; }
            e->name      = decl->as.constDef.name;
            e->constDecl = decl;
            e->next      = scope.consts;
            scope.consts = e;
        } else if (decl->type == NODE_DIRECTIVE_ENUM) {
            EnumEntry *e = malloc(sizeof(EnumEntry));
            if (!e) { *errOut = strdup("out of memory"); scopeFree(&scope); return NULL; }
            e->name     = decl->as.enumDef.name;
            e->enumDecl = decl;
            e->next     = scope.enums;
            scope.enums = e;
        }
    }

    scl_doc_t *doc = malloc(sizeof(scl_doc_t));
    if (!doc) {
        *errOut = strdup("out of memory");
        scopeFree(&scope);
        return NULL;
    }
    arenaInit(&doc->arena);
    doc->root = valueAlloc(&doc->arena, SCL_TYPE_STRUCT);

    for (size_t i = 0; i < astDoc->as.document.count; i++) {
        AstNode *decl = astDoc->as.document.decls[i];
        if (!decl || decl->type != NODE_FIELD) continue;

        scl_value_t *val = valueFromAst(decl->as.field.value,
                                         decl->as.field.typeNode,
                                         &scope,
                                         &doc->arena, errOut);
        if (!val) {
            arenaFree(&doc->arena);
            free(doc);
            scopeFree(&scope);
            return NULL;
        }

        const char *key = arenaStrdup(&doc->arena, decl->as.field.name);
        valueKVSet(doc->root, key, val, &doc->arena);
    }

    scopeFree(&scope);
    return doc;
}
