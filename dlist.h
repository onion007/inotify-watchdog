#define DLISTLOCK char

typedef struct stDLIST
{
    DLISTLOCK      * m_pLock;
    struct stDLIST * m_pPrev;
    struct stDLIST * m_pNext;
}DLIST;


void app_dlistLockInit( DLISTLOCK * pLock );
void app_dlistLock( DLISTLOCK * pLock );
void app_dlistUnlock( DLISTLOCK * pLock );

int  app_dlistInit( DLIST * pNode, DLISTLOCK * pLock );
void app_dlistAddLocked( DLIST * pNewNode, DLIST * pHead );
void app_dlistAdd( DLIST * pNewNode, DLIST * pHead );
void app_dlistDropLocked( DLIST * pNode );
void app_dlistDrop( DLIST * pNode );
int  app_dlistIsEmpty( DLIST * pdlist );
