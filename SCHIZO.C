#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <io.h>
#include <GnuType.h>
#include <GnuArg.h>
#include <GnuMisc.h>
#include <GnuDir.h>


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
   } NODE;
typedef NODE *PNODE;



typedef struct
   {
   PNODE pTree;           // ptr to word tree
   PNODE pStartNode;      // Start of chain
   ULONG ulWords;         // total read words
   ULONG ulWordBytes;     // total mem allocated to string data
   ULONG ulNodes;         // total word nodes
   ULONG ulDupes;         // total node duplicates
   ULONG ulLinks;         // total hit nodes
   } NFO;
typedef NFO *PNFO;


USHORT  uCONVERGENCE;
BOOL    bUSEDIVERGENCE = FALSE;

USHORT  MAXOUTLINES;
USHORT  LINELEN;

PNFO    pnfo = NULL;

char sz[512];        // general str buff
char apszList[4000]; // general mem area (for lists ...)


/*******************************************************************/
/*                                                                 */
/*  Misc Util Functions                                            */
/*                                                                 */
/*******************************************************************/



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



void InitNFO (PNFO pNfo)
   {
   pNfo->pTree       = NULL;
   pNfo->pStartNode  = NULL;
   pNfo->ulWords     = 0;
   pNfo->ulWordBytes = 0;
   pNfo->ulNodes     = 0;
   pNfo->ulDupes     = 0;
   pNfo->ulLinks     = 0;
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


/* Free's the tree data */
void FreeTree (PNODE pTree)
   {
   if (!pTree)
      return;
   FreeTree (pTree->Left);
   FreeTree (pTree->Right);
   FreeNode (pTree);
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

   pnfo->ulNodes++;

   pNode->pszWord   = strdup (pszWord);
   pnfo->ulWordBytes += strlen (pszWord);

   pNode->plnk    = NULL;
   pNode->Left    = NULL;
   pNode->Right   = NULL;
   pNode->uCount  = 0;
   return pNode;
   }


/*
 * normally, this fn will return pNode because a match to pszWord
 * has already been established.
 * but if pNode->uCount >= DEPTH, we see if there is another match 
 * on the left branch. If no, then we create a new node
 * anyway and put it on the left branch. If so we recall 1 level down.
 *
 */
PNODE MatchingNode (PNODE pNode, PSZ pszWord)
   {
   PNODE pNewNode;

   if ((UINT)(Rnd(100)+1) > uCONVERGENCE)
      return pNode;
   
   /*--- duplicate node already created ? ---*/
   if (pNode->Left && !strcmp (pNode->Left->pszWord, pszWord))
      return MatchingNode (pNode->Left, pszWord);
   
   /*--- create duplicate node ---*/
   if (!(pNewNode = CreateNode (pszWord)))
      return NULL;

   pnfo->ulDupes++;

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
   else /*(i<=0)*/
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

   pnfo->ulLinks++;
   pNew->pNode  = pNextNode;
   pNew->Next   = NULL;
   pNew->uCount = 1;

   if (!pHit)
      pNode->plnk = pNew;
   else
      pHit->Next = pNew;
   return 2;
   }


/*******************************************************************/
/*                                                                 */
/*  Tree Traversal and info                                        */
/*                                                                 */
/*******************************************************************/


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



///*
// * returns largest uCount in Node tree
// */
//USHORT MaxOccur (PNODE pNode)
//   {
//   USHORT i, j;
//
//   if (!pNode) return 0;
//   i = MaxOccur (pNode->Left); 
//   j = MaxOccur (pNode->Right); 
//   i = max (i, j);
//   return max (i, pNode->uCount);
//   }


///*
// * returns depth of tree
// */
//USHORT Depth (PNODE pNode)
//   {
//   USHORT i, j;
//
//   if (!pNode) return 0;
//
//   i = Depth (pNode->Left); 
//   j = Depth (pNode->Right); 
//   return 1 + max (i, j);
//   }



USHORT ReadFile (PNFO pNfo, PSZ pszFile)
   {
   char   szWord[MAXWORD];
   PNODE  pNewNode, pOldNode;
   FILE   *fp;

   if (!(fp = fopen (pszFile, "rt")))
      return Error ("Can't open input file %s", pszFile);

   printf ("Reading File: %s .... ", pszFile);

   if (!pNfo->pTree)
      InitNFO (pNfo);

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
      }
   AddALink (pOldNode, NULL); /*--- put end marker on pOldNode ---*/ 

   printf ("%ld Total Words.\n", pNfo->ulWords);
   return 0;
   }



USHORT WriteFile (PNFO pNfo, PSZ pszFile)
   {
   PNODE  pNode;
   FILE   *fp;
   USHORT uLineLen = 0;
   ULONG  ulWords = 0;
   ULONG  ulLines = 0;

   if (!(fp = fopen (pszFile, "wt")))
      return Error ("Can't open output file %s", pszFile);

   printf ("Writing output file: %s ...", pszFile);
   pNode = pNfo->pStartNode;
   do
      {
      uLineLen += strlen (pNode->pszWord) + 1;

      if (uLineLen > LINELEN)
         {
         fputc ('\n', fp);
         uLineLen = strlen (pNode->pszWord) + 1;
         fputs (pNode->pszWord, fp);
         if (++ulLines > MAXOUTLINES)
            break;
         }
      else
         fputs (pNode->pszWord, fp);
      fputc (' ', fp);

      ulWords++;
      }
   while (pNode = NextNode (pNode));
   fputc ('\n', fp);
   fclose (fp);
   printf ("%ld Total Words\n", ulWords);
   return 0;
   }


void Usage ()
   {
   printf ("SCHIZO       Text file scrambler    v2.1                %s  %s\n", __DATE__, __TIME__);
   printf ("\n");
   printf ("USAGE:  SCHIZO [options] InputFile [OutputFile]\n");
   printf ("\n");
   printf ("WHERE:  InputFile .... Text file to read and contemplate.\n");
   printf ("        OutputFile ... The file that is generated.  If not specified the\n");
   printf ("                         InputFile name with a .OUT extension is used.\n");
   printf ("\n");
   printf ("        [options] are 0 or more of:\n");
   printf ("\n");
   printf ("          /Seed=# ......... Specify random number seed.\n");
   printf ("          /Convergence=# .. Specify screwedupidness in percent [0-100].\n");
   printf ("                            100=original file, 0=messed up. Default is 0.\n");
   printf ("          /LineLen=# ...... Max length of each output line.\n");
   printf ("\n");
   printf ("   EXAMPLE: SCHIZO readme.txt\n");
   printf ("   In this example, the file readme.txt is read in and the file readme.out\n");
   printf ("   is created with the text re-arranged.\n");
   exit (0);
   }



int _cdecl main (int argc, char *argv[])
   {
   char   szOutFile [256];
   USHORT uSeed;

   if (ArgBuildBlk ("? *^Seed% *^Convergence% *^MaxLines% *^LineLen%"))
      Error ("%s", ArgGetErr ());

   if (ArgFillBlk (argv))
      Error ("%s", ArgGetErr ());

   if (argc == 1 || ArgIs ("?"))
      Usage ();

   uSeed = (ArgIs ("Seed") ?atoi (ArgGet("Seed", 0)) : (USHORT) time (NULL) % 1000);
   srand (uSeed);

   MAXOUTLINES  = (ArgIs ("MaxLines")    ? atoi (ArgGet ("MaxLines",    0)) : 5000);
   LINELEN      = (ArgIs ("LineLen")     ? atoi (ArgGet ("LineLen",     0)) : 78);
   uCONVERGENCE = (ArgIs ("Convergence") ? atoi (ArgGet ("Convergence", 0)) : 0);

   pnfo = malloc (sizeof (NFO));
   InitNFO (pnfo);

   ReadFile (pnfo, ArgGet (NULL, 0));

   printf ("   Stats:\n");
   printf ("   ------\n");
   printf ("   Words = %ld\n", pnfo->ulWords);
   printf ("   Nodes = %ld\n", pnfo->ulNodes);
   printf ("   Dupes = %ld\n", pnfo->ulDupes);
   printf ("   Links = %ld\n", pnfo->ulLinks);

   DirMakeFileName (szOutFile, ArgGet (NULL, 1), ArgGet (NULL, 0), ".out");
   WriteFile (pnfo, szOutFile);
   return 0;
   }

