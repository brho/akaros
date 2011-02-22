#include "util.h"

int fptr_caller = -1;

void
fptr_report_unexpected_call(int cur)
{
  output("unexpected call %d -> %d\n", fptr_caller, cur);
}

void
fptr_report_unvalidated_return(int cur)
{
  output("unvalidated fptr_caller %d (return to %d)\n", fptr_caller, cur);
}

void
fptr_report_unvalidated_overwrite(int cur)
{
  output("unvalidated fptr_caller %d (overwrite in %d)\n", fptr_caller, cur);
}
