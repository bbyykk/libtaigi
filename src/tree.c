/**
 * tree.c
 *
 * Copyright (c) 1999, 2000, 2001
 *      Lu-chuan Kung and Kang-pen Chen.
 *      All rights reserved.
 *
 * Copyright (c) 2004-2006, 2008, 2010-2014
 *      libchewing Core Team. See ChangeLog for details.
 *
 * See the file "COPYING" for information on usage and redistribution
 * of this file.
 */

/**
 * @file tree.c
 * @brief API for accessing the phrase tree.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "taigi-private.h"
#include "taigi-utf8-util.h"
#include "userphrase-private.h"
#include "global.h"
#include "global-private.h"
#include "dict-private.h"
#include "memory-private.h"
#include "tree-private.h"
#include "private.h"
#include "plat_mmap.h"
#include "taigiutil.h"
#include "key2pho-private.h"

#define INTERVAL_SIZE ( ( MAX_PHONE_SEQ_LEN + 1 ) * MAX_PHONE_SEQ_LEN / 2 )

#ifndef LOG_API_TREE
#undef LOG_API
#undef LOG_VERBOSE
#undef DEBUG_OUT
#define LOG_API(fmt, ...) 
#define LOG_VERBOSE(fmt, ...) 
#define DEBUG_OUT(fmt,...)
#endif

typedef struct PhraseIntervalType {
    int from, to, source;
    Phrase *p_phr;
} PhraseIntervalType;

typedef struct RecordNode {
    int *arrIndex;              /* the index array of the things in "interval" */
    int nInter, score;
    struct RecordNode *next;
    int nMatchCnnct;            /* match how many Cnnct. */
} RecordNode;

typedef struct TreeDataType {
    int leftmost[MAX_PHONE_SEQ_LEN + 1];
    char graph[MAX_PHONE_SEQ_LEN + 1][MAX_PHONE_SEQ_LEN + 1];
    PhraseIntervalType interval[MAX_INTERVAL];
    int nInterval;
    RecordNode *phList;
    int nPhListLen;
} TreeDataType;

static int IsContain(IntervalType in1, IntervalType in2)
{
    return (in1.from <= in2.from && in1.to >= in2.to);
}

int IsIntersect(IntervalType in1, IntervalType in2)
{
    return (max(in1.from, in2.from) < min(in1.to, in2.to));
}

static int PhraseIntervalContain(PhraseIntervalType in1, PhraseIntervalType in2)
{
    return (in1.from <= in2.from && in1.to >= in2.to);
}

static int PhraseIntervalIntersect(PhraseIntervalType in1, PhraseIntervalType in2)
{
    return (max(in1.from, in2.from) < min(in1.to, in2.to));
}

void TerminateTree(ChewingData *pgdata)
{
    pgdata->static_data.tree = NULL;
    plat_mmap_close(&pgdata->static_data.tree_mmap);
}


int InitTree(ChewingData *pgdata, const char *prefix)
{
    char filename[PATH_MAX];
    size_t len;
    size_t offset;

    len = snprintf(filename, sizeof(filename), "%s" PLAT_SEPARATOR "%s", prefix, PHONE_TREE_FILE);
    if (len + 1 > sizeof(filename))
        return -1;

    plat_mmap_set_invalid(&pgdata->static_data.tree_mmap);
    pgdata->static_data.tree_size = plat_mmap_create(&pgdata->static_data.tree_mmap, filename, FLAG_ATTRIBUTE_READ);
    if (pgdata->static_data.tree_size <= 0)
        return -1;

    offset = 0;
    pgdata->static_data.tree =
        (const TreeType *) plat_mmap_set_view(&pgdata->static_data.tree_mmap, &offset, &pgdata->static_data.tree_size);
    if (!pgdata->static_data.tree)
        return -1;

    return 0;
}

static int CheckBreakpoint(int from, int to, int bArrBrkpt[])
{
    int i;

    for (i = from + 1; i < to; i++)
        if (bArrBrkpt[i])
            return 0;
    return 1;
}


int CheckTailoChoose(ChewingData *pgdata,
                           uint32_t *new_phoneSeq, int from, int to,
                           Phrase **pp_phr,
                           char selectStr[][MAX_PHONE_SEQ_LEN * MAX_UTF8_SIZE + 1],
                           IntervalType selectInterval[], int nSelect)
{
    IntervalType inte, c;
    int chno, len;
    int user_alloc;
    UserPhraseData *pTailoPhraseData = NULL;
    Phrase *p_phr = ALC(Phrase, 1);

    assert(p_phr);
    inte.from = from;
    inte.to = to;
    *pp_phr = NULL;

    TRACY("%s, %d\n");
    /* pass 1
     * if these exist one selected interval which is not contained by inte
     * but has intersection with inte, then inte is an unacceptable interval
     */
    for (chno = 0; chno < nSelect; chno++) {
        c = selectInterval[chno];
        if (IsIntersect(inte, c) && !IsContain(inte, c)) {
		goto end;
        }
    }

    /* pass 2
     * if there exist one phrase satisfied all selectStr then return 1, else return 0.
     * also store the phrase with highest freq
     */
    pTailoPhraseData = TailoGetPhraseFirst(pgdata, new_phoneSeq);
    if (pTailoPhraseData == NULL)
      goto end;

    p_phr->freq = -1;
    do {
        for (chno = 0; chno < nSelect; chno++) {
            c = selectInterval[chno];

            if (IsContain(inte, c)) {
                /*
                 * find a phrase of ph_id where the text contains
                 * 'selectStr[chno]' test if not ok then return 0,
                 * if ok then continue to test. */
                len = c.to - c.from;
                if (memcmp(pgdata->tailophrase_data.wordSeq,
                           selectStr[chno], strlen(selectStr[chno]))) {
		    TRACY("%s, %d, selectStr=%s\n", __func__, __LINE__, selectStr[chno]);
                    break;
		}
            }
        }
        if (chno == nSelect) {
            /* save phrase data to "pp_phr" */
	    /* A mistery bug here: the pTailoPhraseData should point to pgdata->tailophrase_data
	     * However, core dump would happen if using pTailoPhraseData->userfreq and other members
	     * But use the full pgdata->tailophrase_data is fine
	     */
            //if (pTailoPhraseData->userfreq > p_phr->freq) {
            if (pgdata->tailophrase_data.userfreq > p_phr->freq) {
                if ((user_alloc = (to - from)) > 0) {
                    strncpy(p_phr->phrase, pgdata->tailophrase_data.wordSeq, 64);
                }
                p_phr->freq = pgdata->tailophrase_data.userfreq;
		p_phr->type = pgdata->tailophrase_data.type;
		if(!p_phr->type)
			p_phr->type = TYPE_TAILO;
                *pp_phr = p_phr;
		TRACY("%s, %d, *pp_phr->phrase=%s\n", __func__, __LINE__, (*pp_phr)->phrase);
            }
        }
    } while ((pTailoPhraseData = TailoGetPhraseNext(pgdata, new_phoneSeq)) != NULL);
    TailoGetPhraseEnd(pgdata, new_phoneSeq);

    if (p_phr->freq != -1)
        return 1;
  end:
    free(p_phr);
    return 0;
}


static int CheckUserChoose(ChewingData *pgdata,
                           uint32_t *new_phoneSeq, int from, int to,
                           Phrase **pp_phr,
                           char selectStr[][MAX_PHONE_SEQ_LEN * MAX_UTF8_SIZE + 1],
                           IntervalType selectInterval[], int nSelect)
{
    IntervalType inte, c;
    int chno, len;
    int user_alloc;
    UserPhraseData *pUserPhraseData;
    Phrase *p_phr = ALC(Phrase, 1);

    assert(p_phr);
    inte.from = from;
    inte.to = to;
    *pp_phr = NULL;
    /* pass 1
     * if these exist one selected interval which is not contained by inte
     * but has intersection with inte, then inte is an unacceptable interval
     */
    for (chno = 0; chno < nSelect; chno++) {
        c = selectInterval[chno];
        if (IsIntersect(inte, c) && !IsContain(inte, c)) {
		goto end;
        }
    }

    /* pass 2
     * if there exist one phrase satisfied all selectStr then return 1, else return 0.
     * also store the phrase with highest freq
     */
    pUserPhraseData = UserGetPhraseFirst(pgdata, new_phoneSeq);
    if (pUserPhraseData == NULL)
      goto end;

    p_phr->freq = -1;
    do {
        for (chno = 0; chno < nSelect; chno++) {
            c = selectInterval[chno];

            if (IsContain(inte, c)) {
                /*
                 * find a phrase of ph_id where the text contains
                 * 'selectStr[chno]' test if not ok then return 0,
                 * if ok then continue to test. */
                len = c.to - c.from;
                if (memcmp(ueStrSeek(pUserPhraseData->wordSeq, c.from - from),
                           selectStr[chno], ueStrNBytes(selectStr[chno], len))) {
                    break;
		}
            }

        }
        if (chno == nSelect) {
            /* save phrase data to "pp_phr" */
            if (pUserPhraseData->userfreq > p_phr->freq) {
                if ((user_alloc = (to - from)) > 0) {
		    TRACX("%s, %d\n", __func__, __LINE__);
                    ueStrNCpy(p_phr->phrase, pUserPhraseData->wordSeq, user_alloc, 1);
                }
                p_phr->freq = pUserPhraseData->userfreq;
		p_phr->type = pUserPhraseData->type;
		if(!p_phr->type)
			p_phr->type = TYPE_HAN;
			
                *pp_phr = p_phr;
		TRACY("%s, %d, pp_phr=0x%x, freq=%d, type=%d\n", __func__, __LINE__,
				(uint32_t) *pp_phr, p_phr->freq, p_phr->type);
            }
        }
    } while ((pUserPhraseData = UserGetPhraseNext(pgdata, new_phoneSeq)) != NULL);
    UserGetPhraseEnd(pgdata, new_phoneSeq);

    if (p_phr->freq != -1)
        return 1;
  end:
    free(p_phr);
    return 0;
}

/*
 * phrase is said to satisfy a choose interval if
 * their intersections are the same */
static int CheckChoose(ChewingData *pgdata,
                       const TreeType *phrase_parent, int from, int to, Phrase **pp_phr,
                       char selectStr[][MAX_PHONE_SEQ_LEN * MAX_UTF8_SIZE + 1],
                       IntervalType selectInterval[], int nSelect)
{
    IntervalType inte, c;
    int chno, len;
    Phrase *phrase = ALC(Phrase, 1);

    assert(phrase);
    inte.from = from;
    inte.to = to;
    *pp_phr = NULL;

    /* if there exist one phrase satisfied all selectStr then return 1, else return 0. */
    GetPhraseFirst(pgdata, phrase, phrase_parent);
    do {
        for (chno = 0; chno < nSelect; chno++) {
            c = selectInterval[chno];

            if (IsContain(inte, c)) {
                /* find a phrase under phrase_parent where the text contains
                 * 'selectStr[chno]' test if not ok then return 0, if ok
                 * then continue to test
                 */
                len = c.to - c.from;
                if (memcmp(ueStrSeek(phrase->phrase, c.from - from),
                           selectStr[chno], ueStrNBytes(selectStr[chno], len))) {
                    break;
		}
            } else if (IsIntersect(inte, selectInterval[chno])) {
		    goto end;
            }
        }
        if (chno == nSelect) {
            *pp_phr = phrase;
            return 1;
        }
    } while (GetVocabNext(pgdata, phrase));
end:
    free(phrase);
    return 0;
}

static int CompTreeType(const void *a, const void *b)
{
    return GetUint32(((TreeType *) a)->key) - GetUint32(((TreeType *) b)->key);
}

/** @brief search for the phrases have the same pronunciation.*/
/* if phoneSeq[begin] ~ phoneSeq[end] is a phrase, then add an interval
 * from (begin) to (end+1)
 */
const TreeType *TreeFindPhrase(ChewingData *pgdata, int begin, int end, const uint32_t *phoneSeq)
{
    TreeType target;
    const TreeType *tree_p = pgdata->static_data.tree;
    uint32_t range[2];
    int i;

    for (i = begin; i <= end; i++) {
        PutUint32(phoneSeq[i], target.key);
        range[0] = GetUint32(tree_p->child.begin);
        range[1] = GetUint32(tree_p->child.end);

	DEBUG_OUT("%s: phoneSeq[%d]=%d, target.key=%d, range[0]=%d, range[1]=%d\n",
		__func__, i, phoneSeq[i], target.key, range[0], range[1]);
        assert(range[1] >= range[0]);
        tree_p = (const TreeType *) bsearch(&target, pgdata->static_data.tree + range[0],
                                            range[1] - range[0], sizeof(TreeType), CompTreeType);

        /* if not found any word then fail. */
        if (!tree_p)
            return NULL;
    }
    /* If its child has no key value of 0, then it is only a "half" phrase. */
    if (GetUint32(pgdata->static_data.tree[GetUint32(tree_p->child.begin)].key) != 0)
        return NULL;
    return tree_p;
}

/**
 * @brief get child range of a given parent node.
 */
void TreeChildRange(ChewingData *pgdata, const TreeType *parent)
{
    TRACX("%s, %d\n", __func__, __LINE__);
    pgdata->static_data.tree_cur_pos = pgdata->static_data.tree + GetUint32(parent->child.begin);
    pgdata->static_data.tree_end_pos = pgdata->static_data.tree + GetUint32(parent->child.end);
}

static void AddInterval(TreeDataType *ptd, int begin, int end, Phrase *p_phrase, int dict_or_user)
{
    TRACX("%s, %d\n", __func__, __LINE__);
    ptd->interval[ptd->nInterval].from = begin;
    ptd->interval[ptd->nInterval].to = end + 1;
    ptd->interval[ptd->nInterval].p_phr = p_phrase;
    ptd->interval[ptd->nInterval].source = dict_or_user;
    ptd->nInterval++;
}

/* Item which inserts to interval array */
typedef enum {
    USED_PHRASE_NONE,           /**< none of items used */
    USED_PHRASE_USER,           /**< User phrase */
    USED_PHRASE_DICT,            /**< Dict phrase */
    USED_PHRASE_TAILO,            /**< Dict phrase */
} UsedPhraseMode;

static void FindInterval(ChewingData *pgdata, TreeDataType *ptd)
{
    int end, begin;
    const TreeType *phrase_parent;
    Phrase *p_phrase, *puserphrase = NULL, *pdictphrase = NULL, *ptailophrase = NULL;
    UsedPhraseMode i_used_phrase = USED_PHRASE_NONE;
    uint32_t new_phoneSeq[MAX_PHONE_SEQ_LEN];
    UserPhraseData *userphrase = NULL, *tailophrase = NULL;

    TRACX("====== %s, %d START, pgdata->nPhoneSeq=%d\n", __func__,__LINE__, pgdata->nPhoneSeq);
    for (begin = 0; begin < pgdata->nPhoneSeq; begin++) {
        for (end = begin; end < min(pgdata->nPhoneSeq, begin + MAX_PHRASE_LEN); end++) {
            if (!CheckBreakpoint(begin, end + 1, pgdata->bArrBrkpt)) {
		TRACX("%s, %d, Break!!\n", __func__, __LINE__);
                break;
	    }

            /* set new_phoneSeq */
            memcpy(new_phoneSeq, &pgdata->phoneSeq[begin], sizeof(uint32_t) * (end - begin + 1));
            new_phoneSeq[end - begin + 1] = 0;
            ptailophrase = puserphrase = pdictphrase = NULL;

            i_used_phrase = USED_PHRASE_NONE;
	    TRACX("XXXXXX pgdata->userphrase_data=0x%x, pgdata->tailophrase_data=0x%x\n",
			    &pgdata->userphrase_data, &pgdata->tailophrase_data);

	    /* Get the Tailo phrase */
            tailophrase = TailoGetPhraseFirst(pgdata, new_phoneSeq);
            TailoGetPhraseEnd(pgdata, new_phoneSeq);

            if (tailophrase && CheckTailoChoose(pgdata, new_phoneSeq, begin, end + 1,
                                              &p_phrase, pgdata->selectStr, pgdata->selectInterval, pgdata->nSelect)) {
                ptailophrase = p_phrase;
		p_phrase = NULL;
		TRACX("%s:%d Get Tailophrase=%s\n", __func__, __LINE__, ptailophrase);
            }
	    /* Get the Tailo phrase -- End */
            userphrase = UserGetPhraseFirst(pgdata, new_phoneSeq);
            UserGetPhraseEnd(pgdata, new_phoneSeq);

            if (userphrase && CheckUserChoose(pgdata, new_phoneSeq, begin, end + 1,
                                              &p_phrase, pgdata->selectStr, pgdata->selectInterval, pgdata->nSelect)) {
                puserphrase = p_phrase;
		p_phrase = NULL;
		TRACX("%s: Get userphrase=%s\n", __func__, puserphrase);
            }

            /* check dict phrase */
            phrase_parent = TreeFindPhrase(pgdata, begin, end, pgdata->phoneSeq);
            if (phrase_parent &&
                CheckChoose(pgdata,
                            phrase_parent, begin, end + 1,
                            &p_phrase, pgdata->selectStr, pgdata->selectInterval, pgdata->nSelect)) {
                pdictphrase = p_phrase;
		p_phrase = NULL;
		TRACX("!!! Get pdictphrase, type=%d, phrase=%s !!!\n", pdictphrase->type, pdictphrase);
	    }

            /* add only one interval, which has the largest freqency
             * but when the phrase is the same, the user phrase overrides
             * static dict
             */
            if (puserphrase != NULL && ptailophrase == NULL && pdictphrase == NULL) {
		TRACX("%s, %d\n", __func__,__LINE__);

                i_used_phrase = USED_PHRASE_USER;

	    } else if (puserphrase == NULL && ptailophrase != NULL && pdictphrase == NULL) {
		TRACX("%s, %d\n", __func__,__LINE__);

                i_used_phrase = USED_PHRASE_TAILO;

            } else if (puserphrase != NULL && ptailophrase != NULL && pdictphrase == NULL) {
		TRACX("%s, %d\n", __func__,__LINE__);

		puserphrase->freq > ptailophrase->freq?(i_used_phrase = USED_PHRASE_USER):(i_used_phrase = USED_PHRASE_TAILO);

            } else if (puserphrase == NULL && ptailophrase == NULL && pdictphrase != NULL) {
		TRACX("%s, %d\n", __func__,__LINE__);

                i_used_phrase = USED_PHRASE_DICT;

            } else if (puserphrase == NULL && ptailophrase != NULL && pdictphrase != NULL) {
		if (ptailophrase->freq > pdictphrase->freq) {
		    i_used_phrase = USED_PHRASE_TAILO;
		} else {
		    i_used_phrase = USED_PHRASE_DICT;
		}
            } else if (puserphrase != NULL && ptailophrase == NULL && pdictphrase != NULL) {
		if (puserphrase->freq > pdictphrase->freq) {
		    i_used_phrase = USED_PHRASE_USER;
		} else {
		    i_used_phrase = USED_PHRASE_DICT;
		}
            } else if (puserphrase != NULL && ptailophrase != NULL && pdictphrase != NULL) {
		if (puserphrase->freq > pdictphrase->freq) {
		    i_used_phrase = USED_PHRASE_USER;
		    if (ptailophrase->freq > puserphrase->freq) {
			    i_used_phrase = USED_PHRASE_TAILO;
		    }
		} else {
		    i_used_phrase = USED_PHRASE_DICT;
		}
            }
            switch (i_used_phrase) {
            case USED_PHRASE_USER:
		TRACY("%s, %d, Using User, phrase=%s\n", __func__,__LINE__, puserphrase->phrase);
                AddInterval(ptd, begin, end, puserphrase, IS_USER_PHRASE);
                break;
            case USED_PHRASE_DICT:
		TRACY("%s, %d, Using Dict, phase=%s\n", __func__,__LINE__, pdictphrase->phrase);
                AddInterval(ptd, begin, end, pdictphrase, IS_DICT_PHRASE);
                break;
            case USED_PHRASE_TAILO:
		TRACY("%s, %d, Using Tailo, phrase=%s\n", __func__,__LINE__, ptailophrase->phrase);
                AddInterval(ptd, begin, end, ptailophrase, IS_TAILO_PHRASE);
                break;
            case USED_PHRASE_NONE:
            default:
	        TRACY("%s, %d, Not Using it\n", __func__,__LINE__);
                break;
            }
		if (puserphrase != NULL && i_used_phrase != USED_PHRASE_USER) {
		    TRACX("XXXXX %s, %d\n", __func__, __LINE__);
		    free(puserphrase);
		    puserphrase = NULL;
		}
		if (pdictphrase != NULL && i_used_phrase != USED_PHRASE_DICT) {
		    TRACX("XXXXX %s, %d\n", __func__, __LINE__);
		    free(pdictphrase);
		    pdictphrase = NULL;
		}
		if (ptailophrase != NULL && i_used_phrase != USED_PHRASE_TAILO) {
		    TRACX("XXXXX %s, %d\n", __func__, __LINE__);
		    free(ptailophrase);
		    ptailophrase = NULL;
		}
	    TRACX("%s, %d XXXXXXXXXXXXXXX\n", __func__, __LINE__);
        }
    }
}

static void SetInfo(int len, TreeDataType *ptd)
{
    int i, a;

    for (i = 0; i <= len; i++)
        ptd->leftmost[i] = i;
    for (i = 0; i < ptd->nInterval; i++) {
        ptd->graph[ptd->interval[i].from][ptd->interval[i].to] = 1;
        ptd->graph[ptd->interval[i].to][ptd->interval[i].from] = 1;
    }

    /* set leftmost */
    for (a = 0; a <= len; a++) {
        for (i = 0; i <= len; i++) {
            if (!(ptd->graph[a][i]))
                continue;
            if (ptd->leftmost[i] < ptd->leftmost[a])
                ptd->leftmost[a] = ptd->leftmost[i];
        }
    }
}

/*
 * First we compare the 'nMatchCnnct'.
 * If the values are the same, we will compare the 'score'
 */
static int CompRecord(const RecordNode **pa, const RecordNode **pb)
{
    int diff = (*pb)->nMatchCnnct - (*pa)->nMatchCnnct;

    if (diff)
        return diff;
    return ((*pb)->score - (*pa)->score);
}

/*
 * Remove the interval containing in another interval.
 *
 * Example:
 * 國民大會 has three interval: 國民, 大會, 國民大會. This function removes
 * 國名, 大會 because 國民大會 contains 國民 and 大會.
 */
static void Discard1(TreeDataType *ptd)
{
    int a, b;
    char failflag[INTERVAL_SIZE];
    int nInterval2;

    memset(failflag, 0, sizeof(failflag));
    for (a = 0; a < ptd->nInterval; a++) {
        if (failflag[a])
            continue;
        for (b = 0; b < ptd->nInterval; b++) {
            if (a == b || failflag[b])
                continue;

            /* interval b is in interval a */
            if (PhraseIntervalContain(ptd->interval[a], ptd->interval[b]))
                continue;

            /* interval b is in front of interval a */
            if (ptd->interval[b].to <= ptd->interval[a].from)
                continue;

            /* interval b is in back of interval a */
            if (ptd->interval[a].to <= ptd->interval[b].from)
                continue;

            break;
        }
        /* if any other interval b is inside or leftside or rightside the
         * interval a */
        if (b >= ptd->nInterval) {
            /* then kill all the intervals inside the interval a */
            int i;

            for (i = 0; i < ptd->nInterval; i++) {
                if (!failflag[i] && i != a && PhraseIntervalContain(ptd->interval[a], ptd->interval[i])) {
                    failflag[i] = 1;
                }
            }
        }
    }
    /* discard all the intervals whose failflag[a] = 1 */
    nInterval2 = 0;
    for (a = 0; a < ptd->nInterval; a++) {
        if (!failflag[a]) {
            ptd->interval[nInterval2++] = ptd->interval[a];
        } else {
            if (ptd->interval[a].p_phr != NULL) {
                free(ptd->interval[a].p_phr);
            }
        }
    }
    ptd->nInterval = nInterval2;
}

/*
 * Remove the interval that cannot connect to head or tail by other intervals.
 *
 * Example:
 * The input string length is 5
 * The available intervals are [1,1], [1,2], [2,3], [2,4], [5,5], [3,5].
 *
 * The possible connection from head to tail are [1,2][3,5], and
 * [1,1][2,4][5,5]. Since [2,3] cannot connect to head or tail, it is removed
 * by this function.
 */
static void Discard2(TreeDataType *ptd)
{
    int i, j;
    char overwrite[MAX_PHONE_SEQ_LEN];
    char failflag[INTERVAL_SIZE];
    int nInterval2;

    memset(failflag, 0, sizeof(failflag));
    for (i = 0; i < ptd->nInterval; i++) {
        if (ptd->leftmost[ptd->interval[i].from] == 0)
            continue;
        /* test if interval i is overwrited by other intervals */
        memset(overwrite, 0, sizeof(overwrite));
        for (j = 0; j < ptd->nInterval; j++) {
            if (j == i)
                continue;
            memset(&overwrite[ptd->interval[j].from], 1, ptd->interval[j].to - ptd->interval[j].from);
        }
        if (memchr(&overwrite[ptd->interval[i].from], 1, ptd->interval[i].to - ptd->interval[i].from))
            failflag[i] = 1;
    }
    /* discard all the intervals whose failflag[a] = 1 */
    nInterval2 = 0;
    for (i = 0; i < ptd->nInterval; i++)
        if (!failflag[i])
            ptd->interval[nInterval2++] = ptd->interval[i];
    ptd->nInterval = nInterval2;
}

static void FillPreeditBuf(ChewingData *pgdata, char *phrase, int from, int to, int type)
{
    int i;
    int start = 0;
    char *str_start = phrase;

    assert(pgdata);
    assert(phrase);
    assert(from < to);

    start = toPreeditBufIndex(pgdata, from);

    TRACX("Fill preeditBuf phrase=%s, type=%d, start = %d, from = %d, to = %d\n", phrase, type, start, from, to);

    for (i = start; i < start - from + to; ++i) {
	if (!pgdata->preeditBuf[i].char_) {
		TRACZ("%s, %d, !!!!! preeditBuf[%d]._char_ is NULL\n", __func__, __LINE__);
		break;
	}
	if (phrase && IsThePhone(phrase[0])) {
		TRACX("%s, %d\n", __func__, __LINE__);
		char *end = strchr(str_start, '-');
		int len = 0;
		if (end) {
			len = end - str_start;
		} else
			len = strlen(str_start);

		memcpy(pgdata->preeditBuf[i].char_, str_start, len);
		pgdata->preeditBuf[i].char_[len] = 0;
		pgdata->preeditBuf[i].type = type;
		pgdata->preeditBuf[i].len = strlen(pgdata->preeditBuf[i].char_);
		str_start = end + 1;
	} else if (phrase && IsTheTaiLoPhone(phrase)) {
		TRACX("%s, %d\n", __func__, __LINE__);
		char *end = strchr(str_start, '-');
		int len = 0;
		if (end) {
			len = end - str_start;
		} else
			len = strlen(str_start);

		memcpy(pgdata->preeditBuf[i].char_, str_start, len);
		pgdata->preeditBuf[i].char_[len] = 0;
		pgdata->preeditBuf[i].type = type;
		pgdata->preeditBuf[i].len = strlen(pgdata->preeditBuf[i].char_);
		str_start = end + 1;
	} else {
		/* Han character */
		TRACX("%s, %d\n", __func__, __LINE__);
		ueStrNCpy(pgdata->preeditBuf[i].char_, ueStrSeek(phrase, i - start), 1, STRNCPY_CLOSE);
		pgdata->preeditBuf[i].type = type;
		pgdata->preeditBuf[i].len = strlen(pgdata->preeditBuf[i].char_);
	}
	LOG_VERBOSE("pgdata->preeditBuf[%d].char_=%s, type=%d", i, pgdata->preeditBuf[i].char_, pgdata->preeditBuf[i].type);
    }
}

/* kpchen said, record is the index array of interval */
static void OutputRecordStr(ChewingData *pgdata, const TreeDataType *ptd)
{
    PhraseIntervalType inter;
    int i;

    LOG_VERBOSE("%d, ptd->phList->nInter=%d\n", __LINE__, ptd->phList->nInter);
    for (i = 0; i < ptd->phList->nInter; i++) {
        inter = ptd->interval[ptd->phList->arrIndex[i]];
        FillPreeditBuf(pgdata, inter.p_phr->phrase, inter.from, inter.to, inter.p_phr->type);
    }
    LOG_VERBOSE("\n%d, pgdata->nSelect=%d\n", __LINE__, pgdata->nSelect);
    /* Not sure the diff between phrase and select, use the last one */
    for (i = 0; i < pgdata->nSelect; i++) {
        FillPreeditBuf(pgdata, pgdata->selectStr[i], pgdata->selectInterval[i].from, pgdata->selectInterval[i].to, pgdata->selectInterval[i].type);
    }
}

static int rule_largest_sum(const int *record, int nRecord, const TreeDataType *ptd)
{
    int i, score = 0;
    PhraseIntervalType inter;

    for (i = 0; i < nRecord; i++) {
        inter = ptd->interval[record[i]];
        assert(inter.p_phr);
        score += inter.to - inter.from;
    }
    return score;
}

static int rule_largest_avgwordlen(const int *record, int nRecord, const TreeDataType *ptd)
{
    /* constant factor 6=1*2*3, to keep value as integer */
    return 6 * rule_largest_sum(record, nRecord, ptd) / nRecord;
}

static int rule_smallest_lenvariance(const int *record, int nRecord, const TreeDataType *ptd)
{
    int i, j, score = 0;
    PhraseIntervalType inter1, inter2;

    /* kcwu: heuristic? why variance no square function? */
    for (i = 0; i < nRecord; i++) {
        for (j = i + 1; j < nRecord; j++) {
            inter1 = ptd->interval[record[i]];
            inter2 = ptd->interval[record[j]];
            assert(inter1.p_phr && inter2.p_phr);
            score += abs((inter1.to - inter1.from) - (inter2.to - inter2.from));
        }
    }
    return -score;
}

static int rule_largest_freqsum(const int *record, int nRecord, const TreeDataType *ptd)
{
    int i, score = 0;
    PhraseIntervalType inter;

    for (i = 0; i < nRecord; i++) {
        inter = ptd->interval[record[i]];
        assert(inter.p_phr);

        /* We adjust the 'freq' of One-word Phrase */
        score += (inter.to - inter.from == 1) ? (inter.p_phr->freq / 512) : inter.p_phr->freq;
    }
    return score;
}

static int LoadPhraseAndCountScore(const int *record, int nRecord, const TreeDataType *ptd)
{
    int total_score = 0;

    /* NOTE: the balance factor is tuneable */
    if (nRecord) {
        total_score += 1000 * rule_largest_sum(record, nRecord, ptd);
        total_score += 1000 * rule_largest_avgwordlen(record, nRecord, ptd);
        total_score += 100 * rule_smallest_lenvariance(record, nRecord, ptd);
        total_score += rule_largest_freqsum(record, nRecord, ptd);
    }
    return total_score;
}

static int IsRecContain(const int *intA, int nA, const int *intB, int nB, const TreeDataType *ptd)
{
    int big, sml;

    for (big = 0, sml = 0; sml < nB; sml++) {
        while ((big < nA) && ptd->interval[intA[big]].from < ptd->interval[intB[sml]].to) {
            if (PhraseIntervalContain(ptd->interval[intA[big]], ptd->interval[intB[sml]]))
                break;
            big++;
        }
        if ((big >= nA) || ptd->interval[intA[big]].from >= ptd->interval[intB[sml]].to)
            return 0;
    }
    return 1;
}

static void SortListByScore(TreeDataType *ptd)
{
    int i, listLen;
    RecordNode *p, **arr;

    for (listLen = 0, p = ptd->phList; p; listLen++, p = p->next);
    ptd->nPhListLen = listLen;

    arr = ALC(RecordNode *, listLen);

    assert(arr);

    for (i = 0, p = ptd->phList; i < listLen; p = p->next, i++) {
        arr[i] = p;
        p->score = LoadPhraseAndCountScore(p->arrIndex, p->nInter, ptd);
    }

    qsort(arr, listLen, sizeof(RecordNode *), (CompFuncType) CompRecord);

    ptd->phList = arr[0];
    for (i = 1; i < listLen; i++) {
        arr[i - 1]->next = arr[i];
    }
    arr[listLen - 1]->next = NULL;

    free(arr);
}

/* when record==NULL then output the "link list" */
static void SaveRecord(const int *record, int nInter, TreeDataType *ptd)
{
    RecordNode *now, *p, *pre;

    printf("<<<< %s, %d, Inter=%d >>>>\n", __func__, __LINE__, nInter);
    pre = NULL;
    for (p = ptd->phList; p;) {
        /* if  'p' contains 'record', then discard 'record'. */
        if (IsRecContain(p->arrIndex, p->nInter, record, nInter, ptd))
            return;

        /* if 'record' contains 'p', then discard 'p'
         * -- We must deal with the linked list. */
        if (IsRecContain(record, nInter, p->arrIndex, p->nInter, ptd)) {
            RecordNode *tp = p;

            if (pre)
                pre->next = p->next;
            else
                ptd->phList = ptd->phList->next;
            p = p->next;
            free(tp->arrIndex);
            free(tp);
        } else
            pre = p, p = p->next;
    }
    now = ALC(RecordNode, 1);

    assert(now);
    now->next = ptd->phList;
    now->arrIndex = ALC(int, nInter);

    assert(now->arrIndex);
    now->nInter = nInter;
    memcpy(now->arrIndex, record, nInter * sizeof(int));
    ptd->phList = now;
}

static void RecursiveSave(int depth, int to, int *record, TreeDataType *ptd)
{
    int first, i;

    /* to find first interval */
    for (first = record[depth - 1] + 1; ptd->interval[first].from < to && first < ptd->nInterval; first++);

    if (first == ptd->nInterval) {
        SaveRecord(record + 1, depth - 1, ptd);
        return;
    }
    record[depth] = first;
    RecursiveSave(depth + 1, ptd->interval[first].to, record, ptd);
    /* for each interval which intersects first */
    for (i = first + 1; PhraseIntervalIntersect(ptd->interval[first], ptd->interval[i]) && i < ptd->nInterval; i++) {
        record[depth] = i;
        RecursiveSave(depth + 1, ptd->interval[i].to, record, ptd);
    }
}

static void SaveList(TreeDataType *ptd)
{
    int record[MAX_PHONE_SEQ_LEN + 1] = { -1 };

    RecursiveSave(1, 0, record, ptd);
}

static void InitPhrasing(TreeDataType *ptd)
{
    memset(ptd, 0, sizeof(TreeDataType));
}

static void SaveDispInterval(PhrasingOutput *ppo, TreeDataType *ptd)
{
    int i;

    for (i = 0; i < ptd->phList->nInter; i++) {
        ppo->dispInterval[i].from = ptd->interval[ptd->phList->arrIndex[i]].from;
        ppo->dispInterval[i].to = ptd->interval[ptd->phList->arrIndex[i]].to;
    }
    ppo->nDispInterval = ptd->phList->nInter;
}

static void CleanUpMem(TreeDataType *ptd)
{
    int i;
    RecordNode *pNode;

    for (i = 0; i < ptd->nInterval; i++) {
        if (ptd->interval[i].p_phr) {
            free(ptd->interval[i].p_phr);
            ptd->interval[i].p_phr = NULL;
        }
    }
    while (ptd->phList != NULL) {
        pNode = ptd->phList;
        ptd->phList = pNode->next;
        free(pNode->arrIndex);
        free(pNode);
    }
}

static void CountMatchCnnct(TreeDataType *ptd, const int *bUserArrCnnct, int nPhoneSeq)
{
    RecordNode *p;
    int i, k, sum;

    for (p = ptd->phList; p; p = p->next) {
        /* for each record, count its 'nMatchCnnct' */
        for (sum = 0, i = 1; i < nPhoneSeq; i++) {
            if (!bUserArrCnnct[i])
                continue;
            /* check if matching 'cnnct' */
            for (k = 0; k < p->nInter; k++) {
                if (ptd->interval[p->arrIndex[k]].from < i && ptd->interval[p->arrIndex[k]].to > i) {
                    sum++;
                    break;
                }
            }
        }
        p->nMatchCnnct = sum;
    }
}

static void ShowList(ChewingData *pgdata, const TreeDataType *ptd)
{
    const RecordNode *p;
    int i;

    DEBUG_OUT("After SaveList :\n");
    for (p = ptd->phList; p; p = p->next) {
        DEBUG_OUT("  interval : ");
        for (i = 0; i < p->nInter; i++) {
            DEBUG_OUT("[%d %d] ", ptd->interval[p->arrIndex[i]].from, ptd->interval[p->arrIndex[i]].to);
        }
        DEBUG_OUT("\n" "      score : %d , nMatchCnnct : %d\n", p->score, p->nMatchCnnct);
    }
    DEBUG_OUT("\n");
}

static RecordNode *NextCut(TreeDataType *tdt, PhrasingOutput *ppo)
{
    /* pop nNumCut-th candidate to first */
    int i;
    RecordNode *former;
    RecordNode *want;

    if (ppo->nNumCut >= tdt->nPhListLen)
        ppo->nNumCut = 0;
    if (ppo->nNumCut == 0)
        return tdt->phList;

    /* find the former of our candidate */
    former = tdt->phList;
    for (i = 0; i < ppo->nNumCut - 1; i++) {
        former = former->next;
        assert(former);
    }

    /* take the candidate out of the listed list */
    want = former->next;
    assert(want);
    former->next = former->next->next;

    /* prepend to front of list */
    want->next = tdt->phList;
    tdt->phList = want;

    return tdt->phList;
}

static int SortByIncreaseEnd(const void *x, const void *y)
{
    const PhraseIntervalType *interval_x = (const PhraseIntervalType *) x;
    const PhraseIntervalType *interval_y = (const PhraseIntervalType *) y;

    if (interval_x->to < interval_y->to)
        return -1;

    if (interval_x->to > interval_y->to)
        return 1;

    return 0;
}

static RecordNode *DuplicateRecordAndInsertInterval(const RecordNode *record, TreeDataType *pdt, const int interval_id)
{
    RecordNode *ret = NULL;

    assert(record);
    assert(pdt);


    TRACX("<<<< %s, %d, interval_id=%d >>>>\n", __func__, __LINE__, interval_id);
    ret = ALC(RecordNode, 1);

    if (!ret)
        return NULL;

    ret->arrIndex = ALC(int, record->nInter + 1);
    if (!ret->arrIndex) {
        free(ret);
        return NULL;
    }
    ret->nInter = record->nInter + 1;
    memcpy(ret->arrIndex, record->arrIndex, sizeof(record->arrIndex[0]) * record->nInter);

    ret->arrIndex[ret->nInter - 1] = interval_id;

    ret->score = LoadPhraseAndCountScore(ret->arrIndex, ret->nInter, pdt);

    return ret;
}

static RecordNode *CreateSingleIntervalRecord(TreeDataType *pdt, const int interval_id)
{
    RecordNode *ret = NULL;

    assert(pdt);

    TRACX("<<<< %s, %d >>>>\n", __func__, __LINE__);
    ret = ALC(RecordNode, 1);

    if (!ret)
        return NULL;

    ret->arrIndex = ALC(int, 1);
    if (!ret->arrIndex) {
        free(ret);
        return NULL;
    }

    ret->nInter = 1;
    ret->arrIndex[0] = interval_id;

    ret->score = LoadPhraseAndCountScore(ret->arrIndex, ret->nInter, pdt);

    return ret;
}

static RecordNode *CreateNullIntervalRecord()
{
    RecordNode *ret = NULL;
    ret = ALC(RecordNode, 1);

    TRACX("<<<< %s, %d >>>>\n", __func__, __LINE__);
    if (!ret)
        return NULL;

    ret->arrIndex = ALC(int, 1);
    if (!ret->arrIndex) {
        free(ret);
        return NULL;
    }

    ret->nInter = 0;
    ret->score = 0;

    return ret;
}

static void FreeRecord(RecordNode *node)
{
    if (node) {
        free(node->arrIndex);
        free(node);
    }
}

static void DoDpPhrasing(ChewingData *pgdata, TreeDataType *pdt)
{
    RecordNode *highest_score[MAX_PHONE_SEQ_LEN] = { 0 };
    RecordNode *tmp;
    int prev_end;
    int end;
    int interval_id;

    assert(pgdata);
    assert(pdt);

    /*
     * Assume P(x,y) is the highest score phrasing result from x to y. The
     * following is formula for P(x,y):
     *
     * P(x,y) = MAX( P(x,y-1)+P(y-1,y), P(x,y-2)+P(y-2,y), ... )
     *
     * While P(x,y-1) is stored in highest_score array, and P(y-1,y) is
     * interval end at y. In this formula, x is always 0.
     *
     * The format of highest_score array is described as following:
     *
     * highest_score[0] = P(0,0)
     * highest_score[1] = P(0,1)
     * ...
     * highest_score[y-1] = P(0,y-1)
     */

    /* The interval shall be sorted by the increase order of end. */
    {
	    int i;
	    for (i = 0; i < pgdata->nSelect; i++) {
		TRACZ("@@@@@@ %s, %d, pgdata->selectStr[%d]=%s\n", __func__, __LINE__, i, pgdata->selectStr[i]);
	    }
    }
    qsort(pdt->interval, pdt->nInterval, sizeof(pdt->interval[0]), SortByIncreaseEnd);

    for (interval_id = 0; interval_id < pdt->nInterval; ++interval_id) {
        /*
         * XXX: pdt->interval.to is excluding, while end is
         * including, so we need to minus one here.
         */
        end = pdt->interval[interval_id].to - 1;

        prev_end = pdt->interval[interval_id].from - 1;

        if (prev_end >= 0) {
	    TRACZ("@@@@@@ %s, %d, highest_score[%d], interval_id=%d\n", __func__, __LINE__,  prev_end, interval_id);
            tmp = DuplicateRecordAndInsertInterval(highest_score[prev_end], pdt, interval_id);
	}
        else {
            tmp = CreateSingleIntervalRecord(pdt, interval_id);
	}

        /* FIXME: shall exit immediately? */
        if (!tmp)
            continue;

        if (highest_score[end] == NULL || highest_score[end]->score < tmp->score) {
            FreeRecord(highest_score[end]);
            highest_score[end] = tmp;
        } else
            FreeRecord(tmp);
    }

    if (pgdata->nPhoneSeq - 1 < 0 || highest_score[pgdata->nPhoneSeq - 1] == NULL) {
	TRACX("-->>-->> %s, %d, pgdata->nPhoneSeq=%d\n", __func__, __LINE__, pgdata->nPhoneSeq);
        pdt->phList = CreateNullIntervalRecord();
    } else {
        pdt->phList = highest_score[pgdata->nPhoneSeq - 1];
    }
    pdt->nPhListLen = 1;

    for (end = 0; end < pgdata->nPhoneSeq - 1; ++end)
        FreeRecord(highest_score[end]);

    {
	    int i;
	    for (i = 0; i < pgdata->nSelect; i++) {
		TRACZ("@@@@@@ %s, %d, pgdata->selectStr[%d]=%s\n", __func__, __LINE__, i, pgdata->selectStr[i]);
	    }
    }
}

int Phrasing(ChewingData *pgdata, int all_phrasing)
{
    TreeDataType treeData;

    DEBUG_OUT("\n");
    TRACY("^^^^^ %s, %d, all_pharseing=%d\n", __func__, __LINE__, all_phrasing);
    InitPhrasing(&treeData);

    FindInterval(pgdata, &treeData);
    SetInfo(pgdata->nPhoneSeq, &treeData);
    Discard1(&treeData);
    Discard2(&treeData);
    if (all_phrasing) {
	TRACY("%s, %d\n", __func__, __LINE__);
        SaveList(&treeData);
        CountMatchCnnct(&treeData, pgdata->bUserArrCnnct, pgdata->nPhoneSeq);
        SortListByScore(&treeData);
        NextCut(&treeData, &pgdata->phrOut);
    } else {
	TRACY("%s, %d\n", __func__, __LINE__);
        DoDpPhrasing(pgdata, &treeData);
    }

    ShowList(pgdata, &treeData);

    TRACY("%s, %d\n", __func__, __LINE__);
    {
	    int i;
	    for (i = 0; i < pgdata->nSelect; i++) {
		TRACZ("@@@@@@ %s, %d, pgdata->selectStr[%d]=%s\n", __func__, __LINE__, i, pgdata->selectStr[i]);
	    }
    }
    /* set phrasing output */
    OutputRecordStr(pgdata, &treeData);
    {
	    int i;
	    for (i = 0; i < pgdata->nSelect; i++) {
		TRACZ("@@@@@@ %s, %d, pgdata->selectStr[%d]=%s\n", __func__, __LINE__, i, pgdata->selectStr[i]);
	    }
    }
    SaveDispInterval(&pgdata->phrOut, &treeData);

    /* free "phrase" */
    CleanUpMem(&treeData);
    TRACY("%s, %d\n", __func__, __LINE__);
    return 0;
}
