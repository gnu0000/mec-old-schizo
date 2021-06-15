#define INCL_VIO
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <GnuArg.h>
#include <GnuScr.h>
#include <GnuKbd.h>
#include <GnuMisc.h>


#define MAXWORD 256
#define TIME __TIME__
#define DATE __DATE__
#define DEFEXT  "OUT"

typedef struct _LNK
   {
   struct _NODE  *pNode;
   struct _LNK   *Next;
   USHORT         uCount;
   } LNK;
typedef LNK *PLNK;


typedef struct _NODE 
   {
   PSZ          pszWord;
   PLNK         plnk;
   struct _NODE *Left;
   struct _NODE *Right;
   USHORT       uCount;
   USHORT       uIdx;
   } NODE;
typedef NODE *PNODE;



typedef struct
   {
   PSZ   pszInFile;       // latest input file
   PSZ   pszOutFile;      // out file name
   PNODE pTree;           // ptr to word tree
   PNODE pStartNode;      // Start of chain
   ULONG ulWords;         // total read words
   ULONG ulWordBytes;     // total mem allocated to string data
   ULONG ulNodes;         // total word nodes
   ULONG ulDupes;      // total node duplicates
   ULONG ulLinks;          // total hit nodes
   } NFO;
typedef NFO *PNFO;


BOOL    BQUIET       = FALSE;
BOOL    BREPLACE     = FALSE;
BOOL    BQUERYDELETE = TRUE;
USHORT  PERSONALITY  = 65535;
USHORT  MAXOUTLINES  = 2000;
USHORT  SAMPLESIZE   = 512;
USHORT  OUTLEN       = 78;
USHORT  PROGRESSINTERVAL = 100;



NFO nfo;             // Global info struct

PGW pgwL;            // list on left (word list)
PGW pgwR;            // list on right (association list)

char sz[512];        // general str buff
char apszList[4000]; // general mem area (for lists ...)

/*******************************************************************/
/*                                                                 */
/*  Misc Util Functions                                            */
/*                                                                 */
/*******************************************************************/

USHORT OKError (PSZ psz1, PSZ psz2)
   {
   return 1 + GnuMsg ("[Error]", psz1, psz2);
   }


/*  RETURN 0 - abort operation
 *         1 - continue
 */
USHORT AskQuit (PNFO pNfo)
   {
   int     c;

   c = GnuMsgBox2 ("[Exit Window]", NULL, "YN\x1B", 0, 0, 3, 1,
               "Are you sure you want to quit (Y/N)");
   if (c == 'Y')
      return TRUE;
   return FALSE;
   }



void InitNFO (PNFO pNfo)
   {
   pNfo->pszInFile  = NULL; 
   pNfo->pszOutFile = NULL;
   pNfo->pTree      = NULL;
   pNfo->pStartNode = NULL;
   pNfo->ulWords    = 0;
   pNfo->ulWordBytes= 0;
   pNfo->ulNodes    = 0;
   pNfo->ulDupes = 0;
   pNfo->ulLinks     = 0;
   }


/*
 * Loads a whitespace delimited word from fp
 * returns FALSE at eof
 */
int EatWrd (FILE *fp, PSZ pszWord)
   {
   int   c;

   /*--- Skip white space ---*/
   while ((c=getc (fp)) == ' ' || c == '\n' || c =='\t')
      ;
   if (c == EOF)
      return FALSE;
   *pszWord++ = (char)c;

   /*--- read while char is part of word ---*/
   while ((c=getc (fp)) != EOF && !strchr (" \n\t", c))
      *pszWord++ = (char)c;

   *pszWord = '\0';
   return (c ? c : TRUE);
   }



PNODE FreeNode (PNODE pNode)
   {
   PLNK  plnk, plnkNext;

   if (!pNode) return NULL;

   free (pNode->pszWord);
   plnk = pNode->plnk;
   while (plnk)
      {
      plnkNext = plnk->Next;
      free (plnk);
      plnk = plnkNext;
      }
   return NULL;
   }


/*******************************************************************/
/*                                                                 */
/*  Tree Creation and manipulation                                 */
/*                                                                 */
/*******************************************************************/

/* Free's the tree data */
void FreeTree (PNODE pTree)
   {
   if (!pTree)
      return;
   FreeTree (pTree->Left);
   FreeTree (pTree->Right);
   FreeNode (pTree);
   }



/*
 * This Numbers the uIdx field of the tree (for painting)
 * This returns the count of nodes (+ called value - 1)
 */
USHORT NumberTree (PNODE pNode, USHORT uIdx)
   {
   if (!pNode) return uIdx;
   uIdx = NumberTree (pNode->Left, uIdx);
   pNode->uIdx = uIdx++;
   return NumberTree (pNode->Right, uIdx);
   }




PNODE FindNode (PNODE pNode, PSZ pszWord)
   {
   int i;

   if (!pNode)
      return NULL;
   else if (!(i=strcmp (pszWord, pNode->pszWord)))
      return pNode;
   else if (i>0)
      return FindNode (pNode->Right, pszWord);
   else /*(i<0)*/
      return FindNode (pNode->Left, pszWord);
   }



/*
 * Creates and initializes a new tree node
 * returns NULL on an out of mem condition
 */
PNODE CreateNode (PSZ pszWord)
   {
   PNODE pNode;

   if (!(pNode = malloc (sizeof (NODE))))
      return NULL;
   nfo.ulNodes++;

   pNode->pszWord   = strdup (pszWord);
   nfo.ulWordBytes += strlen (pszWord);

   pNode->plnk    = NULL;
   pNode->Left    = NULL;
   pNode->Right   = NULL;
   pNode->uCount  = 0;
   return pNode;
   }


/*
 * normally, this fn will return pNode because a match to pszWord
 * has already been established.
 * but if pNode->uCount*10 > PERSONALITY, we see if there is another match 
 * on the left branch. If no, then we create a new node
 * anyway and put it on the left branch. If so we recall 1 level down.
 *
 */
PNODE MatchingNode (PNODE pNode, PSZ pszWord)
   {
   PNODE pNewNode;

   /*--- definately ok to use this node ---*/
   if (PERSONALITY >= pNode->uCount * 10)
      return pNode;

   /*--- maybe we can use this node ---*/
   if ((PERSONALITY+10 >= pNode->uCount * 10) &&
       (PERSONALITY+(rand () % 10) >= pNode->uCount * 10))
         return pNode;

   /*--- duplicate node already created ? ---*/
   if (pNode->Left && !strcmp (pNode->Left->pszWord, pszWord))
      return MatchingNode (pNode->Left, pszWord);

   /*--- create duplicate node ---*/
   if (!(pNewNode = CreateNode (pszWord)))
      return NULL;

   nfo.ulDupes++;

   /*--- insert duplicate node on left side of tree ---*/
   pNewNode->Left = pNode->Left;
   pNode->Left = pNewNode;
   return pNewNode;
   }


/*
 * adds a word to a tree
 * either creates a node or returns ptr to existing node
 * returns NULL if no mem
 */
PNODE AddWord (PNODE pNode, PNODE *ppNewNode, PSZ pszWord)
   {
   int   i;

   if (!pNode)
      pNode = *ppNewNode = CreateNode (pszWord);

   else if (!(i=strcmp (pszWord, pNode->pszWord)))
      *ppNewNode = pNode = MatchingNode (pNode, pszWord);
   else if (i>0)
      pNode->Right = AddWord (pNode->Right, ppNewNode, pszWord);
   else /*(i<0)*/
      pNode->Left = AddWord (pNode->Left, ppNewNode, pszWord);
   return pNode;
   }


 
/*
 * Add a hit record to a node
 * returns 0 if no mem
 *
 * 0 - out of mem
 * 1 - added count to existing node
 * 2 - created new node
 */
USHORT AddALink (PNODE pNode, PNODE pNextNode)
   {
   PLNK pNew, pHit;

   pNode->uCount++;
   pHit = pNode->plnk;
   while (pHit)
      {
      if (pHit->pNode == pNextNode)
         {
         pHit->uCount++;
         return 1;
         }
      if (!pHit->Next)
         break;
      pHit = pHit->Next;
      }

   if (!(pNew = malloc (sizeof (LNK))))
      {
      pNode->uCount--;
      return 0;
      }

   nfo.ulLinks++;
   pNew->pNode  = pNextNode;
   pNew->Next   = NULL;
   pNew->uCount = 1;

   if (!pHit)
      pNode->plnk = pNew;
   else
      pHit->Next = pNew;
   return 2;
   }


void LoadProgress (PGW pgw,      PNFO pNfo,
                   ULONG ulWord, ULONG ulNode,
                   ULONG ulDupe, ULONG ulLink)
   {
   sprintf (sz, "%ld", pNfo->ulWords - ulWord);
   GnuPaint (pgw, 2, 15, 0, 1, sz);
   sprintf (sz, "%ld", pNfo->ulNodes - ulNode);
   GnuPaint (pgw, 3, 15, 0, 1, sz);
   sprintf (sz, "%ld", pNfo->ulNodes - ulNode + pNfo->ulDupes - ulDupe);
   GnuPaint (pgw, 4, 15, 0, 1, sz);
   sprintf (sz, "%ld", pNfo->ulLinks - ulLink);
   GnuPaint (pgw, 5, 15, 0, 1, sz);
   }           


/*
 * Adds file data to word tree
 */
USHORT BuildWordTree (PNFO pNfo, PSZ pszFile)
   {
   PGW    pgw;
   char   szWord[MAXWORD];
   PNODE  pNewNode, pOldNode;
   FILE   *fp;
   ULONG  ulWord, ulNode, ulDupNode, ulLink;

   if (!(fp = fopen (pszFile, "rt")))
      return OKError ("Can't open input file", pszFile);

   if (!pNfo->pTree)
      InitNFO (pNfo);

   pNfo->pszInFile = strdup (pszFile);
   ulWord   = pNfo->ulWords;
   ulNode   = pNfo->ulNodes;
   ulDupNode= pNfo->ulDupes;
   ulLink   = pNfo->ulLinks;

   /*--- create progress window ---*/
   ScrSaveCursor (FALSE);
   pgw = GnuCreateWin (11, 50, NULL);
   pgw->pszHeader = (pNfo->pTree ? "[Adding File]" : "[Loading File]");
   GnuPaintBorder (pgw);
   GnuPaintBig (pgw, 1, 0, 5, 14, 1, 0, 
     "Loading File:\nTotal Words:\nNew Words:\nNew Nodes:\nNew Links:");
   GnuPaint (pgw, 7, 0, 3, 0, "Working ...");
   GnuPaint (pgw, 1, 15, 0, 1, pszFile);
   LoadProgress (pgw, pNfo, 0, 0, 0, 0);

   EatWrd (fp, szWord);
   pNewNode = AddWord (pNfo->pTree, &pOldNode, szWord);
   if (!pNfo->pTree) 
      pNfo->pTree = pNewNode;
   if (!pNfo->pStartNode) 
      pNfo->pStartNode = pNewNode;
   pNfo->ulWords++;

   while (EatWrd (fp, szWord) && pNewNode)
      {
      AddWord (pNfo->pTree, &pNewNode, szWord);
      if (!AddALink (pOldNode, pNewNode))
         break;                             // out of mem
      pOldNode = pNewNode;
      pNfo->ulWords++;
      if (!((pNfo->ulWords-ulWord) % PROGRESSINTERVAL))
         LoadProgress (pgw, pNfo, ulWord, ulNode, ulDupNode, ulLink);
      }
   AddALink (pOldNode, NULL); /*--- put end marker on pOldNode ---*/ 
   LoadProgress (pgw, pNfo, ulWord, ulNode, ulDupNode, ulLink);
   GnuPaint (pgw, 7, 0, 3, 0, "Load Complete. Press <Return> to continue");
   GetKey ("\x1B\x0D");
   GnuDestroyWin (pgw);
   ScrRestoreCursor ();

   return 0;
   }


/*******************************************************************/
/*                                                                 */
/*  Tree Traversal and info                                        */
/*                                                                 */
/*******************************************************************/



/*
 * given an Idx, this returns its Node ptr
 */
PNODE NodeByIdx (PNODE pNode, USHORT uIdx)
   {
   if (!pNode) return NULL;

   if (uIdx > pNode->uIdx)
      return NodeByIdx (pNode->Right, uIdx);
   if (uIdx < pNode->uIdx)
      return NodeByIdx (pNode->Left, uIdx);
   return pNode;
   }


/*
 * given an Rel pos, this returns its Hit ptr
 */
PLNK  HitByIdx (PNODE pNode, USHORT uIdx)
   {
   USHORT i;
   PLNK   plnk;

   if (!pNode) return NULL;

   plnk = pNode->plnk;
   for (i=uIdx; i && plnk; i--)
      plnk = plnk->Next;
   return plnk;
   }


/*
 * returns the # of hit nodes for a node
 */
USHORT HitNodes (PNODE pNode)
   {
   USHORT i = 0;
   PLNK   plnk;

   for (plnk = pNode->plnk; plnk; i++)
      plnk = plnk->Next;
   return i;
   }




/*
 * gets the next linked node
 * for the next word.
 */
PNODE NextNode (PNODE pNode)
   {
   USHORT i;
   PLNK   plnk;

   i = rand () % pNode->uCount;

   if (!(plnk = pNode->plnk))
      return NULL;

   while (i >= plnk->uCount)
      {
      i -= plnk->uCount;
      plnk = plnk->Next;
      }
   return plnk->pNode;
   }


/*
 * returns largest uCount in Node tree
 */
USHORT MaxOccur (PNODE pNode)
   {
   USHORT i, j;

   if (!pNode) return 0;
   i = MaxOccur (pNode->Left); 
   j = MaxOccur (pNode->Right); 
   i = max (i, j);
   return max (i, pNode->uCount);
   }


/*
 * returns depth of tree
 */
USHORT Depth (PNODE pNode)
   {
   USHORT i, j;

   if (!pNode) return 0;

   i = Depth (pNode->Left); 
   j = Depth (pNode->Right); 
   return 1 + max (i, j);
   }




void WriteProgress (PGW pgw, ULONG ulWords, ULONG ulLines)
   {
   sprintf (sz, "%ld", ulWords);
   GnuPaint (pgw, 2, 15, 0, 1, sz);
   sprintf (sz, "%ld", ulLines);
   GnuPaint (pgw, 3, 15, 0, 1, sz);
   }



/*
 * This produces the output
 * The head of the tree is also the beginning of the 
 * word chain.  We go until we reach the end word.
 */
USHORT RunTree (PNFO pNfo, PSZ pszFile)
   {
   PNODE  pNode;
   PGW    pgw;
   FILE   *fp;
   USHORT uLineLen = 0;
   ULONG  ulWords = 0;
   ULONG  ulLines = 0;

   if (!(fp = fopen (pszFile, "wt")))
      return OKError ("Can't open output file", pszFile);

   /*--- create progress window ---*/
   ScrSaveCursor (FALSE);
   pgw = GnuCreateWin (9, 50, NULL);
   pgw->pszHeader = "[Writing File]";
   GnuPaintBorder (pgw);
   GnuPaintBig (pgw, 1, 0, 5, 14, 1, 0, 
     "Writing File:\n      Words:\n       Lines:");
   GnuPaint (pgw, 5, 0, 3, 0, "Working ...");
   GnuPaint (pgw, 1, 15, 0, 1, pszFile);
   WriteProgress (pgw, ulWords, ulLines);

   pNode = pNfo->pStartNode;
   do
      {
      uLineLen += strlen (pNode->pszWord) + 1;

      if (uLineLen > OUTLEN)
         {
         fputs (pNode->pszWord, fp);
         if (++ulLines > MAXOUTLINES)
            break;
         fputc ('\n', fp);
         uLineLen = strlen (pNode->pszWord) + 1;
         }
      else
         fputs (pNode->pszWord, fp);
      fputc (' ', fp);

      ulWords++;
      if (!(ulWords % PROGRESSINTERVAL))
         WriteProgress (pgw, ulWords, ulLines);
      }
   while (pNode = NextNode (pNode));
   WriteProgress (pgw, ulWords, ulLines+1);
   fputc ('\n', fp);
   fclose (fp);

   GnuPaint (pgw, 5, 0, 3, 0, "Write Complete. Press <Return> to continue");
   GetKey ("\x1B\x0D");
   GnuDestroyWin (pgw);
   ScrRestoreCursor ();
   return 0;
   }






BOOL MakeSample (PSZ psz, PNODE pNode, USHORT uMaxLen)
   {
   USHORT uLen, uStrLen;

   *psz = '\0';
   if (!pNode || !psz)
      return FALSE;
   do
      {
      uLen = strlen (pNode->pszWord);

      if ((uStrLen += uLen + 1) >= uMaxLen)
         break;

      strcpy (psz, pNode->pszWord);
      psz += uLen;
      *psz++ = ' ';
      }
   while (pNode = NextNode (pNode));
   *psz = '\0';
   return TRUE;
   }


/*******************************************************************/
/*                                                                 */
/*                                                       */
/*                                                                 */
/*******************************************************************/
/*******************************************************************/
/*                                                                 */
/*                                                       */
/*                                                                 */
/*******************************************************************/


/************************************************************************/
/*                                                                      */
/*  Base Screen Painting                                                */
/*                                                                      */
/************************************************************************/


USHORT PaintLeftWin (PGW pgw, USHORT uIdx, USHORT uLine)
   {
   USHORT uAtt, uLen = 0;
   PNODE  pTree, pNode;

   pTree = (PNODE)(PVOID)pgw->pUser1;

   if (!pTree || uIdx >= pgw->uItemCount)
      return 0;

   uAtt = (uIdx == pgw->uSelection && pgw->pUser2 ? 2 : 0);

   if (pNode = NodeByIdx (pTree, uIdx))
      {
      if (pNode == nfo.pStartNode)
         uAtt = (uIdx == pgw->uSelection && pgw->pUser2 ? 3 : 1);

      sprintf (sz, " %5.5d - ", pNode->uCount);
      GnuPaint (pgw, uLine, 0, 0, uAtt, sz);
      uLen = GnuPaint2 (pgw, uLine, 9, 0, uAtt, pNode->pszWord, pgw->uClientXSize-10) + 9;
      }
   if (uLen < pgw->uClientXSize) 
      GnuPaintNChar (pgw, uLine, uLen, 0, uAtt, ' ', pgw->uClientXSize - uLen);
   return 1;
   }



USHORT PaintRightWin (PGW pgw, USHORT uIdx, USHORT uLine)
   {
   USHORT uAtt, uLen = 0;
   PNODE  pNode;
   PLNK   plnk;

   pNode = (PNODE)(PVOID)pgw->pUser1;

   if (!pNode || uIdx >= pgw->uItemCount)
      return 0;

   uAtt = (uIdx == pgw->uSelection && pgw->pUser2 ? 2 : 0);

   if (plnk = HitByIdx (pNode, uIdx))
      {
      sprintf (sz, " %5.5d - ", plnk->uCount);

      if (!plnk->pNode)
         {
         uAtt = (uIdx == pgw->uSelection && pgw->pUser2 ? 3 : 1);
         GnuPaint (pgw, uLine, 0, 0, uAtt, sz);
         uLen = GnuPaint (pgw, uLine, 9, 0, uAtt, "[End Marker] ") + 9;
         }
      else
         {
         GnuPaint (pgw, uLine, 0, 0, uAtt, sz);
         uLen = GnuPaint2 (pgw, uLine, 9, 0, uAtt, plnk->pNode->pszWord, pgw->uClientXSize-10) + 9;
         }
      }
   if (uLen < pgw->uClientXSize) 
      GnuPaintNChar (pgw, uLine, uLen, 0, uAtt, ' ', pgw->uClientXSize - uLen);
   return 1;
   }


void RefreshLabels (PNFO pNfo)
   {
   PNODE pNode;

   if (pNode = pgwR->pUser1)
      {
      GnuPaintNChar (NULL, 3, 62, 0, 1, ' ', 17);
      GnuPaint2     (NULL, 3, 62, 0, 1, pNode->pszWord, 17);
      }
   sprintf (sz, "%5.5d", pgwL->uItemCount);
      GnuPaint (NULL, 3, 24, 0, 1, sz);
   }


void RefreshStatic (PNFO pNfo)
   {
   PMET pmet;

   pmet = ScrGetMetrics ();
   GnuPaint (NULL, 0, 12, 1, 0, " Input File:");
   GnuPaint (NULL, 1, 12, 1, 0, "Output File:");

   GnuPaint (NULL, 3, 13, 0, 0, "Word List (00000)");
   GnuPaint (NULL, 3, 45, 0, 0, "Chain List for: ");

   GnuPaint (NULL, pmet->uYSize-2, 1, 1, 0, "F1-Help   F3-Save      F5-Stats    F7-Clear       F9-    F11- ");
   GnuPaint (NULL, pmet->uYSize-1, 1, 1, 0, "F2-Load   F4-Options   F6-Sample   F8-Start/End   F10-   F12- Esc-Quit");

   if (!pNfo)
      return;
   GnuPaint (NULL, 0, 13, 0, 1, pNfo->pszInFile);
   GnuPaint (NULL, 1, 13, 0, 1, pNfo->pszOutFile);
   RefreshLabels (pNfo);
   }


void RefreshLists ()
   {
   GnuPaintWin (pgwL, 0xFFFF);
   GnuPaintWin (pgwR, 0xFFFF);
   }


void CreateScreen ()
   {
   PMET pmet;

   /*--- cls ---*/
   GnuClearWin (NULL, ' ', 0, FALSE);

   pmet = ScrGetMetrics();

   GnuPaintAtCreate (FALSE);
   pgwL = GnuCreateWin2 (4, 0, pmet->uYSize-8, 40, PaintLeftWin);
   pgwR = GnuCreateWin2 (4, 40, pmet->uYSize-8, 40, PaintRightWin);
   gnuFreeDat (pgwR);
   gnuFreeDat (pgwL);
   pgwL->bShowScrollPos = TRUE;
   pgwR->bShowScrollPos = TRUE;
   pgwL->b3Dch = pgwL->b3Datt = 0xFF;
   pgwR->b3Dch = pgwR->b3Datt = 0xFF;
   GnuSetBorderChars (pgwR, "Ú¿ÀÙÄ³Å");
   GnuClearWin (pgwL, ' ', 0, TRUE);
   GnuClearWin (pgwR, ' ', 0, TRUE);
   GnuPaintAtCreate (TRUE);
   RefreshStatic (NULL);
   RefreshLists ();
   ScrShowCursor (FALSE);
   pgwL->pUser2 = (PVOID) TRUE; /*-- IE. Its active --*/
   }




/************************************************************************/
/*                                                                      */
/*  popup windows                                                       */
/*                                                                      */
/************************************************************************/



void SetRightWin (PNFO pNfo)
   {
   PNODE pNode;

   pNode = NodeByIdx (pNfo->pTree, pgwL->uSelection);
   pgwR->pUser1 = (PVOID) pNode;
   pgwR->uItemCount = HitNodes (pNode);

   if (pgwR->uSelection == 0xFFFF)
      GnuSelectLine (pgwR, (pgwR->uItemCount ? 0 : 0xFFFF), TRUE);
   else
      GnuSelectLine (pgwR, min (pgwR->uSelection , pgwR->uItemCount - 1), TRUE);

   RefreshLabels (pNfo);
   GnuPaintWin (pgwR, 0xFFFF);
   }


/*
 * reads in a text file
 * appends to existing word list if it exists
 * 
 */
USHORT LoadFileWindow (PNFO pNfo, PSZ pszFile)
   {
   char    szFile [128];
   PSZ     pszHeader;

   *szFile='\0';
   if (pszFile)
      strcpy (szFile, pszFile);

   /*--- Query user for file name if not given ---*/
   if (!*szFile)
      {
      strcpy (szFile, "*.*");
      pszHeader = (pNfo ? "[Append File]" : "[Load File]");
      if (!GnuFileWindow (szFile, szFile, pszHeader, (PSZ)apszList))
         return 0;
      }

   if (BuildWordTree (pNfo, szFile))
      return 0;

   NumberTree (pNfo->pTree, 0);
   pgwL->pUser1     = (PVOID) pNfo->pTree;
   pgwL->uItemCount = (USHORT)pNfo->ulNodes;
   GnuSelectLine (pgwL, 0, TRUE);
   RefreshStatic (pNfo);
   GnuPaintWin (pgwL, 0xFFFF);
   SetRightWin (pNfo);
   RefreshLabels (pNfo);
   return 0;
   }



USHORT WriteFileWindow (PNFO pNfo, PSZ pszFile)
   {
   char    szFile [128];
   PSZ     p;
   PGW     pgw;
   USHORT  uRet;

   *szFile='\0';
   if (pszFile) strcpy (szFile, pszFile);

   /*--- Query user for file name if not given ---*/
   if (!*szFile)
      {
      if (pNfo->pszOutFile)
         strcpy (szFile, pNfo->pszOutFile);
      else
         {
         strcpy (szFile, pNfo->pszInFile);
         if (p = strrchr (szFile, '.'))  *p = '\0';
         strcat (szFile, ".");
         strcat (szFile, DEFEXT);
         }
      pgw = GnuCreateWin (8, 50, NULL);
      pgw->pszHeader = "[Write File]";
      GnuPaintBorder (pgw);

      GnuPaint (pgw, 1, 0, 3, 0, "Enter Output File Name:");
      GnuPaint (pgw, 5, 0, 3, 0, " Then press <Enter> to save or <ESC> to abort ");

      uRet = EditCell (szFile, pgw->uClientYPos+3, pgw->uClientXPos+3, 40, 1, "\x1B\x0D", "");
      GnuDestroyWin (pgw);

      if (!uRet)
         return 0;
      }
   return RunTree (pNfo, szFile);
   }




/************************************************************************/
/*                                                                      */
/*  key processing                                                      */
/*                                                                      */
/************************************************************************/


USHORT AddLinkWindow (PNFO pNfo, PNODE pNode)
   {
   PGW    pgw;
   PNODE  pNewNode;
   char   szNewWord [MAXWORD];
   USHORT uRet;

   ScrSaveCursor (TRUE);
   pgw = GnuCreateWin (11, 60, NULL);
   pgw->pszHeader = "[Add Word Link]";
   pgw->pszFooter = "[Press <Esc> to cancel]";
   GnuPaintBorder (pgw);

   GnuPaint (pgw, 1, 0, 3, 0, "Enter new word link for");
   GnuPaint (pgw, 2, 0, 3, 1, pNode->pszWord);
   GnuPaint (pgw, 3, 0, 3, 0, "The new word must already exist in the word list");
   GnuPaint (pgw, 4, 0, 3, 0, "type \"<NULL>\" to create a terminator marker");

   while (TRUE)
      {
      *szNewWord = '\0';
      uRet = EditCell (szNewWord, TopOf(pgw)+7, LeftOf(pgw)+10, 40, 0, "\x1B\x0D", "");
      if (uRet == '\x1B')
         {
         GnuDestroyWin (pgw);
         ScrRestoreCursor ();
         return 0;
         }
      if (pNewNode = FindNode (pNfo->pTree, szNewWord))
         break;
      GnuPaint (pgw, 8, 0, 3, 0, "Word does not exist!");
      }
   if (AddALink (pNode, pNewNode) == 2) /*--- new link added ---*/
      pgwR->uItemCount++;

   RefreshLists ();
   GnuDestroyWin (pgw);
   ScrRestoreCursor ();
   return 0;
   }


USHORT DelLinkWindow (PNFO pNfo, PNODE pNode)
   {
   PLNK plnk, plnkTmp;

   if (!(plnk = HitByIdx (pNode, pgwR->uSelection)))
      return 1- Beep (0);

   if (BQUERYDELETE)
      {
      sprintf (sz, "Delete Link: \\@01%s\\@00\n  From Word: \\@01%s\\@00\n\nPress <Enter> to delete",
                  plnk->pNode->pszWord, pNode->pszWord);
      if (GnuMsgBox ("[Delete Link]", "[Press <Esc> to abort]", "\x1B\x0D", sz) == '\x1B')
         return 0;
      }
   if (plnk == (plnkTmp = pNode->plnk))
      pNode->plnk = plnk->Next;
   else
      {
      for (; plnkTmp->Next != plnk; plnkTmp = plnkTmp->Next)
         ;
      plnkTmp->Next = plnk->Next;
      }
   pNode->uCount -= plnk->uCount;
   free (plnk);
   pgwR->uItemCount--;
   RefreshLists ();
   return 0;
   }

USHORT MoveLinkUp (PNODE pNode, PLNK plnk)
   {
   PLNK plnk1;
   PNODE  pn;
   USHORT i;

   if (!pNode || !plnk || plnk == pNode->plnk)
      return 1- Beep(0);

   for (plnk1 = pNode->plnk; plnk1->Next != plnk; plnk1=plnk1->Next)
         ;
   pn=plnk1->pNode , plnk1->pNode =plnk->pNode ,plnk->pNode =pn; 
   i =plnk1->uCount, plnk1->uCount=plnk->uCount,plnk->uCount=i;

   GnuSelectLine (pgwR, pgwR->uSelection-1, TRUE);

//   GnuPaintWin (pgwR, pgwR->uSelection);
//   GnuPaintWin (pgwR, pgwR->uSelection-1);
   return 0;
   }


USHORT MoveLinkDn (PNODE pNode, PLNK plnk)
   {
   PLNK   plnk1;
   PNODE  pn;
   USHORT i;

   if (!pNode || !plnk || !plnk->Next)
      return 1- Beep(0);

   plnk1 = plnk->Next;
   pn=plnk1->pNode , plnk1->pNode =plnk->pNode ,plnk->pNode =pn; 
   i =plnk1->uCount, plnk1->uCount=plnk->uCount,plnk->uCount=i;
   GnuSelectLine (pgwR, pgwR->uSelection+1, TRUE);
//   GnuPaintWin (pgwR, pgwR->uSelection);
//   GnuPaintWin (pgwR, pgwR->uSelection+1);
   return 0;
   }

USHORT AddWordWindow (PNFO pNfo, PNODE pNode)
   {
   return 0;
   }

USHORT DelWordWindow (PNFO pNfo, PNODE pNode)
   {
   return 0;
   }



int RightKbdLoop (PNFO pNfo, int c)
   {
   USHORT uSel;
   PNODE  pNode;
   PLNK   plnk;

   uSel  = pgwR->uSelection;
   pNode = (PNODE) pgwR->pUser1;
   plnk  = HitByIdx (pNode, uSel);

   if (uSel == 0xFFFF || !pNode)
      return 1 - Beep (0);

   switch (c)
      {
      case '+':                             /*--- + ---*/
         if (plnk)
            {
            plnk->uCount++;   
               GnuPaintWin (pgwR, uSel);
            pNode->uCount++;
            GnuPaintWin (pgwL, pgwL->uSelection);
            }
         else
            Beep (0);
         break;

      case '-':                             /*--- - ---*/
         if (plnk && plnk->uCount) 
            {
            plnk->uCount--;   
            GnuPaintWin (pgwR, uSel);
            pNode->uCount--;
            GnuPaintWin (pgwL, pgwL->uSelection);
            }
         else
            Beep (0);
         break;

      case 0x152:                           /*--- ins ---*/
         AddLinkWindow (pNfo, pNode);
         break;

      case 0x153:                           /*--- del ---*/
         DelLinkWindow (pNfo, pNode);
         break;

      case 0x184:                           /*--- Ctl-Pg-Up ---*/
         MoveLinkUp (pNode, plnk);
         break;

      case 0x176:                           /*--- Ctl-Pg-Dn ---*/
         MoveLinkDn (pNode, plnk);
         break;

      case 0x142:                         /*------ F8 Add/Remove End Mark ---*/
         {
         PLNK plnkOld = NULL;

         for (plnk = pNode->plnk; plnk && plnk->pNode; plnk = plnk->Next)
            plnkOld = plnk;
         if (plnk)
            {
            if (plnkOld)
               plnkOld->Next = plnk->Next;
            else
               pNode->plnk = plnk->Next;
            pNode->uCount -= plnk->uCount;
            free (plnk);
            pgwR->uItemCount--;
            RefreshLists ();
            }
         else
            {
            if (AddALink (pNode, NULL) == 2) /*--- new link added ---*/
               pgwR->uItemCount++;
            RefreshLists ();
            }
         }
         break;

      default:
         return 1 - Beep (0);
      }
   return 0;
   }


int LeftKbdLoop (PNFO pNfo, int c)
   {
   USHORT i, uSel;
   PNODE  pNode;
   PLNK   plnk;

   uSel  = pgwL->uSelection;
   pNode = NodeByIdx (pNfo->pTree, uSel);
   plnk  = pNode->plnk;

   if (!pNode || uSel >= pgwL->uItemCount)
      return 0;

   switch (c)
      {
      case '+':                             /*--- + ---*/
         while (plnk)
            {
            plnk->uCount++;
            pNode->uCount++;
            plnk = plnk->Next;
            }
         GnuPaintWin (pgwL, uSel);
         GnuPaintWin (pgwR, 0xFFFF);
         break;

      case '-':                             /*--- - ---*/
         while (plnk)
            {
            if (plnk->uCount)
               {
               plnk->uCount--;
               pNode->uCount--;
               }
            plnk = plnk->Next;
            }
         GnuPaintWin (pgwL, uSel);
         GnuPaintWin (pgwR, 0xFFFF);
         break;


      case 0x182:                           /*--- ins ---*/
         AddWordWindow (pNfo, pNode);
         break;

      case 0x183:                           /*--- del ---*/
         DelWordWindow (pNfo, pNode);
         break;

      case 0x142:                           /*--- F8 move start Mark ---*/
         i = pNfo->pStartNode->uIdx;
         pNfo->pStartNode = pNode;
         GnuPaintWin (pgwL, i);
         GnuPaintWin (pgwL, uSel);
         break;

      default:
         return 1 - Beep (0);
      }
   return 0;
   }


void KbdLoop (PNFO pNfo)
   {
   int      c, iNext = 0;
   USHORT   uFocus, uStoreSel=0xFFFF;
   PGW      pgw[2];

   pgw[0] = pgwL;
   pgw[1] = pgwR;

   uFocus = 0; /*--- LeftWindow ---*/

   while (TRUE)
      {
      c = (iNext ? iNext : mygetch ());

      switch (c)
         {
         case 0x13b:                         /*------ F1     Help    ------*/
            break;

         case 0x13c:                         /*------ F2     Load    ------*/
            LoadFileWindow (pNfo, NULL);
            break;

         case 0x13d:                         /*------ F3     Save    ------*/
            WriteFileWindow (pNfo, NULL);
            break;

         case 0x13e:                         /*------ F4     Options ------*/
            break;

         case 0x13f:                         /*------ F5     Stats ------*/
            break;

         case 0x140:                         /*------ F6     Sample ------*/
            {
            PNODE pNode;
            PLNK  plnk;

            if (uFocus)
               {
               if ((pNode=(PNODE)pgwR->pUser1) && (plnk=HitByIdx(pNode, pgwR->uSelection)))
                  pNode = plnk->pNode;
               else
                  pNode = NULL;
               }
            else
               pNode = NodeByIdx (pNfo->pTree, pgwL->uSelection);

            if (!MakeSample (apszList, pNode, SAMPLESIZE))
               Beep (0);
            else
               GnuMsgBox ("[Sample Chain]", "[Press <Enter to continue>]", "\x0D\x1b", apszList);
            break;
            }

         case 0x141:                         /*------ F7     Clear ------*/
            {
            int     c;

            c = GnuMsgBox2 ("[Clear Data]", NULL, "YN\x1B", 0, 0, 3, 1,
                        "Are you sure you want to clear all data (Y/N)");
            if (c == 'Y')
               {
               }
            break;

         case 0x143:                         /*------ F9 ---*/
            break;

         case 0x144:                         /*------ F10 ---*/
            break;


         case 0x118:                         /*------ Alt-O  Colors  ------*/
            GnuSetColorWindow (NULL);   //-- set colors dialog ---//
            GnuSetColors (pgwL, 0xFFFF, 0, 0);
            GnuSetColors (pgwR, 0xFFFF, 0, 0);
            RefreshStatic (pNfo);
            RefreshLists  ();
            break;

         case 0x09:                          /*------ <TAB>          ------*/
            GnuSetBorderChars (pgw[uFocus], "Ú¿ÀÙÄ³Å");
            GnuPaintBorder (pgw[uFocus]);
            uFocus = 1-uFocus;
            GnuSetBorderChars (pgw[uFocus], NULL);
            GnuPaintBorder (pgw[uFocus]);

            /*--- dont show selection on non-active window ---*/
            (pgw[uFocus])->pUser2   = (PVOID)TRUE;
            (pgw[1-uFocus])->pUser2 = (PVOID)FALSE;
            GnuPaintWin (pgwL, pgwL->uSelection);
            GnuPaintWin (pgwR, pgwR->uSelection);

            if (uFocus)
               SetRightWin (pNfo);
            break;

         case 'U':
            SetRightWin (pNfo);
            break;

         case 27:                            /*------ F10    Exit    ------*/
         case 0x11:                          /*------ Ctl-Q          ------*/
         case 0x18:                          /*------ Ctl-X          ------*/
         case 0x12d:                         /*------ Alt-X          ------*/
            if (AskQuit (pNfo))
                  exit (0);
            break;

         default:
            if (GnuDoListKeys (pgw[uFocus], c)) /*------ Move Selection ------*/
               {
               /*---fixup other win ---*/
               break;
               }
            if (uFocus)
               iNext = RightKbdLoop (pNfo, c);
            else
               iNext = LeftKbdLoop (pNfo, c);
         }
      }
   }


_cdecl main (int argc, char *argv[])
   {
   ScrInitMetrics ();

   GnuSetColors (NULL, 0, 10, 15); // client fg
   GnuSetColors (NULL, 1, 10, 10); // border fg
   GnuSetColors (NULL, 2, 1,  5);  // window bg

   CreateScreen ();
   InitNFO (&nfo);
   KbdLoop (&nfo);
   return 0;
   }



