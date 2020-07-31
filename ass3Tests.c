#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

#define STDOUT 1


void
welcomeprint()
{
  printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  printf(STDOUT,"~~~~~~~STARTING SIGNAL TESTS~~~~~~~\n");
  printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
}

void
doneprint()
{
  printf(STDOUT,"\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  printf(STDOUT,"~~~~~~~~~~~ALL TESTS PASSED~~~~~~~~\n");
  printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
}

void printStatistics()
{
    int memoryPages = 0;
    int swapPages = 0;
    int pageFaults = 0;
    int pagedOut = 0;

    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);

    printf(1, "Memory Pages\tPaged Out\tPage Faults\tTotal Paged Out\n");
    printf(1, "%d\t\t%d\t\t%d\t\t%d\n\n", memoryPages, swapPages, pageFaults, pagedOut);
}

void fail(char *msg)
{
    printStatistics();
    printf(1, "TEST FAILED : %s\n", msg);
    exit();
}

void T1()
{
    printf(STDOUT,"~~~~~~~~~~~~~~~TEST1~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"Fork and Change Child Data\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

    int pages = 5;
    char *buffer = sbrk(4096 * pages);

    for (int i = 0; i < pages; i++)
        buffer[i * 4096] = 'A';   //filled with A
        
    if (fork() == 0)    {   //if son
        for (int i = 0; i < pages; i++)
            printf(1, "child data: %c\n", buffer[i * 4096]);

        for (int i = 0; i < pages; i++)
            buffer[i * 4096] = 'B';   //filled with B

        for (int i = 0; i < pages; i++)
            printf(1, "child data after change: %c\n", buffer[i * 4096]);    //should be B

        sbrk(-4096 * pages);

        exit();
    }
    else    {   //if father
        sleep(75);
        for (int i = 0; i < pages; i++){
            printf(1, "father data: %c\n", buffer[i * 4096]);   //should be A
            wait();
        }

        sbrk(-4096 * pages);

        printf(STDOUT,"\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        printf(STDOUT,"~~~~~~~~~~~TEST1 PASSED~~~~~~~~~~~~\n");
        printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
    }
}


void T2()
{
    printf(STDOUT,"~~~~~~~~~~~~~~~TEST2~~~~~~~~~~~~~~\n");
    printf(STDOUT,"Before and after Freeing Buffer\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");


    printf(1, "before allocating:\n");
    printStatistics();

    int numberOfPages = 16;
    sbrk(4096 * numberOfPages);

    printf(1, "after allocating:\n");
    printStatistics();
    sbrk(-(4096 * numberOfPages));

    printf(1, "after freeing buffer:\n");
    printStatistics();

    printf(STDOUT,"\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~TEST2 PASSED~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
}

void T3()
{
    printf(STDOUT,"~~~~~~~~~~~~~~~TEST3~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"Allocate 20 pages\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

    int memoryPages = null;
    int swapPages = null;
    int pageFaults = null;
    int pagedOut = null;
    int numberOfPages = 20;

    sbrk(4096 * numberOfPages);
    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);
    printStatistics();
    if (memoryPages < 16)    {   //should be less than 16 pages on memory
        sbrk(-(4096 * numberOfPages));
        fail("maximum 16 pages on memory");
    }
    if (swapPages < 4)    {
        sbrk(-(4096 * numberOfPages));   //should be 4 pages in swapfile
        fail("should have 4 pages in swapfile");
    }
    sbrk(-(4096 * numberOfPages));

    printf(STDOUT,"\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~TEST3 PASSED~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
}

void T4()
{
    printf(STDOUT,"~~~~~~~~~~~~~~~TEST4~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"Fill Memory Partly, Swapfile is Empty\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");

    int memoryPages = null;
    int swapPages = null;
    int pageFaults = null;
    int pagedOut = null;
    int numberOfPages = 5;   // fill memory partly, and swapfile is empty
    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);

    int oldNumberofSwapPages = swapPages;   //check cuuret swapfile and memory pages
    int oldNumberofRamPages = memoryPages;
    printf(1, "before allocate:\n");
    printStatistics();

    char *buffer = sbrk(4096 * numberOfPages);
    printf(1, "after allocate:\n");
    printStatistics();

    for (int i = 0; i < numberOfPages; i++)
    {
        strcpy(&buffer[i * 4096], "test");
    }
    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);

    if (memoryPages < oldNumberofRamPages + 5)   //check allocating 5 pages on memory
        fail("did not allocate pages in memory");

    if (swapPages > oldNumberofSwapPages)   //check no new allocation on swapfile
        fail("should not allocate pages in swapfile");

    for (int i = 0; i < numberOfPages; i++)
    {
        if (strcmp("write", &buffer[i * 4096]) < 0)
            fail("failed to write\n");   //try to write
    }
    printf(1, "after writing:\n");
    printStatistics();
    free(buffer);
    sbrk(-(4096 * numberOfPages));

    printf(STDOUT,"\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~TEST4 PASSED~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
}
// OnPageFault2: same as OnPagefault 1 on 'full ram'
void T5()
{
    printf(STDOUT,"~~~~~~~~~~~~~~~TEST5~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"Fill Memory Completley, Swapfile Partly\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");

    int memoryPages = null;
    int swapPages = null;
    int pageFaults = null;
    int pagedOut = null;
    int numberOfPages = 16; // fill memory completely, and swapfile has space

    printf(1, "before allocate:\n");
    printStatistics();

    char *buffer = sbrk(4096 * numberOfPages);
    printf(1, "after allocate:\n");
    printStatistics();

    for (int i = 0; i < numberOfPages; i++)
    {
        strcpy(&buffer[i * 4096], "test");
    }

    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);

    if (memoryPages < 16)   //check memory is full
        fail("memory should be full\n");

    if (swapPages <= 0)   //check swapfile have pages
        fail("swapfile should have pages saved on it\n");

    if (pagedOut < 0)   //check we have pagedout
        fail(" should have at least one paged out\n");

    int oldPagedOut = pagedOut;

    for (int i = 0; i < numberOfPages; i++)
    {
        if (strcmp("write", &buffer[i * 4096]) < 0)
            fail("failed to write\n"); //try to write
    }
    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);

    if (oldPagedOut == pagedOut)   //check we have more paged out
        fail("should have paged out more pages after reading\n");

    if (pageFaults <= 0)   //check we have page faults
        fail(" should have at least one page fault\n");

    printf(1, "after writing:\n");
    printStatistics();

    sbrk(-(4096 * numberOfPages));

    printf(STDOUT,"\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~TEST5 PASSED~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
}

void T6()
{
    printf(STDOUT,"~~~~~~~~~~~~~~~TEST6~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"Fill Memory and Swapfile Completely\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");

    int memoryPages = null;
    int swapPages = null;
    int pageFaults = null;
    int pagedOut = null;
    int numberOfPages;

    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);
    numberOfPages = (32 - (memoryPages + swapPages));
    printf(1, "number of pages to allocate: %d\n\n", numberOfPages);
    printf(1, "before allocate:\n");
    printStatistics();

    char *buffer = sbrk(4096 * numberOfPages);   //fill memory and swapfile
    printf(1, "after allocate:\n");
    printStatistics();

    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);
    if (memoryPages < 16)   //check memory is full
        fail("memory should be full\n");

    if (swapPages < 16)   //check swapfile is full
        fail("swapfile should be full\n");

    if (pagedOut < 0)   //check we have pagedout
        fail("should have at least one paged out\n");

    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);
    int oldPagedOut = pagedOut;

    for (int i = 0; i < numberOfPages; i++)   //try to write
    {
        if (strcmp("write", &buffer[i * 4096]) < 0)
            fail("failed to write\n");
    }
    statistics(&memoryPages, &swapPages, &pageFaults, &pagedOut);

    if (oldPagedOut == pagedOut)   //check we have more paged out
        fail("should have paged out more pages\n");

    if (pageFaults <= 0)   //check we have page faults
        fail("should have at least one page fault\n");

    printf(1, "after writing:\n");
    printStatistics();

    sbrk(-(4096 * numberOfPages));

    printf(STDOUT,"\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~TEST6 PASSED~~~~~~~~~~~~\n");
    printf(STDOUT,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
}

#define PGSIZE 4096
#define ARR_SIZE_FORK PGSIZE*20
#define ARR_SIZE_TEST PGSIZE*20


int main(void){
    welcomeprint();
    T1();
    T2();
    T3();
    T4();
    T5();
    T6();
    doneprint();
    exit();
}