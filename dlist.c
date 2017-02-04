#include <unistd.h>
#include "dlist.h"


void app_dlistLockInit( DLISTLOCK * pLock )
{
}

void app_dlistLock( DLISTLOCK * pLock )
{
}

void app_dlistUnlock( DLISTLOCK * pLock )
{
}

int app_dlistInit( DLIST * pNode, DLISTLOCK * pLock )
{
    if( NULL == pNode )
    {
        return -1;
    }

    pNode->m_pLock = pLock;
    pNode->m_pPrev = pNode;
    pNode->m_pNext = pNode;

    return 0;
}

void app_dlistAddLocked( DLIST * pNewNode, DLIST * pHead )
{
    if( NULL == pNewNode || NULL == pHead )
    {
        return;
    }

    pNewNode->m_pLock = pHead->m_pLock;
    pNewNode->m_pPrev = pHead->m_pPrev;
    pNewNode->m_pNext = pHead;

    pHead->m_pPrev->m_pNext = pNewNode;
    pHead->m_pPrev          = pNewNode;
}

void app_dlistAdd( DLIST * pNewNode, DLIST * pHead )
{
    if( NULL == pNewNode || NULL == pHead )
    {
        return;
    }

    if( pHead->m_pLock )
    {
        app_dlistLock( pHead->m_pLock );
    }
    app_dlistAddLocked( pNewNode, pHead );
    if( pHead->m_pLock )
    {
        app_dlistUnlock( pHead->m_pLock );
    }
}

void app_dlistDropLocked( DLIST * pNode )
{
    if( NULL == pNode )
    {
        return;
    }

    pNode->m_pPrev->m_pNext = pNode->m_pNext;
    pNode->m_pNext->m_pPrev = pNode->m_pPrev;

    pNode->m_pNext = pNode;
    pNode->m_pPrev = pNode;
}

void app_dlistDrop( DLIST * pNode )
{
    if( NULL == pNode )
    {
        return;
    }

    if( pNode->m_pLock )
    {
        app_dlistLock( pNode->m_pLock );
    }
    app_dlistDropLocked( pNode );
    if( pNode->m_pLock )
    {
        app_dlistUnlock( pNode->m_pLock );
    }
}

/*
 * RETURN:
 *    1. TRUE. EMPTY
 *    0. FALSE. NOT EMPTY
 */
int app_dlistIsEmpty( DLIST * pdlist )
{
    if( NULL == pdlist )
    {
        return 1;
    }

    if( pdlist->m_pNext != pdlist )
    {
        return 0;
    }

    return 1;
}
