#include <stdio.h>
//16655
#include <syscall.h>

int
main (int argc, char **argv)
{
  int i;

  for (i = 0; i < argc; i++)
    printf ("%s ", argv[i]);
  printf ("\n");

  return EXIT_SUCCESS;
}
