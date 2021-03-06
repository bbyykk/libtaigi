/**
 * chewing-sql.c
 *
 * Copyright (c) 2013
 *      libchewing Core Team. See ChangeLog for details.
 *
 * See the file "COPYING" for information on usage and redistribution
 * of this file.
 */

/* *INDENT-OFF* */
#ifndef CHEWING_SQL_H
#define CHEWING_SQL_H
/* *INDENT-ON* */

#ifdef HAVE_CONFIG_H
#    include <config.h>
#endif


/*
 * userphrase_v1 table
 */

enum {
    BIND_USERPHRASE_TIME = 1,
    BIND_USERPHRASE_ORIG_FREQ = 2,
    BIND_USERPHRASE_MAX_FREQ = 3,
    BIND_USERPHRASE_USER_FREQ = 4,
    BIND_USERPHRASE_LENGTH = 5,
    BIND_USERPHRASE_PHRASE = 6,
    BIND_USERPHRASE_TYPE = 7,
    BIND_USERPHRASE_PHONE_0 = 10,
    BIND_USERPHRASE_PHONE_1 = 11,
    BIND_USERPHRASE_PHONE_2 = 12,
    BIND_USERPHRASE_PHONE_3 = 13,
    BIND_USERPHRASE_PHONE_4 = 14,
    BIND_USERPHRASE_PHONE_5 = 15,
    BIND_USERPHRASE_PHONE_6 = 16,
    BIND_USERPHRASE_PHONE_7 = 17,
    BIND_USERPHRASE_PHONE_8 = 18,
    BIND_USERPHRASE_PHONE_9 = 19,
    BIND_USERPHRASE_PHONE_10 = 20,
};

enum {
    COLUMN_USERPHRASE_TIME,
    COLUMN_USERPHRASE_ORIG_FREQ,
    COLUMN_USERPHRASE_MAX_FREQ,
    COLUMN_USERPHRASE_USER_FREQ,
    COLUMN_USERPHRASE_LENGTH,
    COLUMN_USERPHRASE_PHRASE,
    COLUMN_USERPHRASE_TYPE,
    COLUMN_USERPHRASE_PHONE_0,
    COLUMN_USERPHRASE_PHONE_1,
    COLUMN_USERPHRASE_PHONE_2,
    COLUMN_USERPHRASE_PHONE_3,
    COLUMN_USERPHRASE_PHONE_4,
    COLUMN_USERPHRASE_PHONE_5,
    COLUMN_USERPHRASE_PHONE_6,
    COLUMN_USERPHRASE_PHONE_7,
    COLUMN_USERPHRASE_PHONE_8,
    COLUMN_USERPHRASE_PHONE_9,
    COLUMN_USERPHRASE_PHONE_10,
    COLUMN_USERPHRASE_COUNT,
};

enum {
    STMT_USERPHRASE_SELECT,
    STMT_USERPHRASE_SELECT_BY_PHONE,
    STMT_USERPHRASE_SELECT_BY_PHONE_PHRASE,
    STMT_USERPHRASE_UPSERT,
    STMT_USERPHRASE_DELETE,
    STMT_USERPHRASE_GET_MAX_FREQ,
    STMT_USERPHRASE_COUNT,
};

typedef struct SqlStmtUserphrase_ {
    const char *stmt;
    const char column[COLUMN_USERPHRASE_COUNT];
} SqlStmtUserphrase;


/*
 * tailo_v1 table
 */

enum {
    BIND_TAILOPHRASE_TIME = 1,
    BIND_TAILOPHRASE_ORIG_FREQ = 2,
    BIND_TAILOPHRASE_MAX_FREQ = 3,
    BIND_TAILOPHRASE_USER_FREQ = 4,
    BIND_TAILOPHRASE_LENGTH = 5,
    BIND_TAILOPHRASE_PHRASE = 6,
    BIND_TAILOPHRASE_TYPE = 7,
    BIND_TAILOPHRASE_PHONE_0 = 10,
    BIND_TAILOPHRASE_PHONE_1 = 11,
    BIND_TAILOPHRASE_PHONE_2 = 12,
    BIND_TAILOPHRASE_PHONE_3 = 13,
    BIND_TAILOPHRASE_PHONE_4 = 14,
    BIND_TAILOPHRASE_PHONE_5 = 15,
    BIND_TAILOPHRASE_PHONE_6 = 16,
    BIND_TAILOPHRASE_PHONE_7 = 17,
    BIND_TAILOPHRASE_PHONE_8 = 18,
    BIND_TAILOPHRASE_PHONE_9 = 19,
    BIND_TAILOPHRASE_PHONE_10 = 20,
};

enum {
    COLUMN_TAILOPHRASE_TIME,
    COLUMN_TAILOPHRASE_ORIG_FREQ,
    COLUMN_TAILOPHRASE_MAX_FREQ,
    COLUMN_TAILOPHRASE_USER_FREQ,
    COLUMN_TAILOPHRASE_LENGTH,
    COLUMN_TAILOPHRASE_PHRASE,
    COLUMN_TAILOPHRASE_TYPE,
    COLUMN_TAILOPHRASE_PHONE_0,
    COLUMN_TAILOPHRASE_PHONE_1,
    COLUMN_TAILOPHRASE_PHONE_2,
    COLUMN_TAILOPHRASE_PHONE_3,
    COLUMN_TAILOPHRASE_PHONE_4,
    COLUMN_TAILOPHRASE_PHONE_5,
    COLUMN_TAILOPHRASE_PHONE_6,
    COLUMN_TAILOPHRASE_PHONE_7,
    COLUMN_TAILOPHRASE_PHONE_8,
    COLUMN_TAILOPHRASE_PHONE_9,
    COLUMN_TAILOPHRASE_PHONE_10,
    COLUMN_TAILOPHRASE_COUNT,
};

enum {
    STMT_TAILOPHRASE_SELECT,
    STMT_TAILOPHRASE_SELECT_BY_PHONE,
    STMT_TAILOPHRASE_SELECT_BY_PHONE_PHRASE,
    STMT_TAILOPHRASE_UPSERT,
    STMT_TAILOPHRASE_DELETE,
    STMT_TAILOPHRASE_GET_MAX_FREQ,
    STMT_TAILOPHRASE_COUNT,
};

typedef struct SqlStmtTailophrase_ {
    const char *stmt;
    const char column[COLUMN_TAILOPHRASE_COUNT];
} SqlStmtTailophrase;


/*
 * config_v1 table
 */

enum {
    BIND_CONFIG_ID = 1,
    BIND_CONFIG_VALUE = 2,
};

enum {
    COLUMN_CONFIG_ID,
    COLUMN_CONFIG_VALUE,
    COLUMN_CONFIG_COUNT,
};

enum {
    STMT_CONFIG_SELECT,
    STMT_CONFIG_INSERT,
    STMT_CONFIG_INCREASE,
    STMT_CONFIG_COUNT,
};

enum {
    CONFIG_ID_LIFETIME,
};

typedef struct SqlStmtConfig_ {
    const char *stmt;
    const char column[COLUMN_CONFIG_COUNT];
} SqlStmtConfig;

extern const SqlStmtUserphrase SQL_STMT_USERPHRASE[STMT_USERPHRASE_COUNT];
extern const SqlStmtUserphrase SQL_STMT_TAILOPHRASE[STMT_TAILOPHRASE_COUNT];

struct ChewingData;

int InitUserphrase(struct ChewingData *pgdata, const char *path);
void TerminateUserphrase(struct ChewingData *pgdata);

/* *INDENT-OFF* */
#endif
/* *INDENT-ON* */
