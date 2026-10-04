/* Compile the native adapter directly so its actual SDL callback can be invoked
 * with ephemeral native result buffers without displaying a file chooser. */
#include "../../src/platform/windows/dialogs.c"
#include "../test.h"
static int received;
static void receive(void *userdata, const char *path, const PPError *error) {
  CHECK(userdata == &received);
  CHECK(!error && path && !strcmp(path, "C:/temporary/\xc3\xa9.pointy"));
  received++;
}
int main(void) {
  PPDialogRequest *request = calloc(1, sizeof(*request));
  CHECK(request);
  request->callback = receive;
  request->userdata = &received;
  requests = request;
  char path[] = "C:/temporary/\xc3\xa9.pointy";
  const char *files[] = {path, NULL};
  dialog_result(request, files, 0);
  memset(path, 'x', sizeof(path) - 1);
  CHECK(!received);
  pp_platform_poll_dialogs();
  CHECK(received == 1 && !requests);
  request = calloc(1, sizeof(*request));
  CHECK(request);
  request->callback = receive;
  request->userdata = &received;
  requests = request;
  pp_platform_cancel_file_dialogs(&received);
  const char *cancel[] = {NULL};
  dialog_result(request, cancel, 0);
  pp_platform_poll_dialogs();
  CHECK(received == 1 && !requests);
  puts("dialog_queue: borrowed result copied, main-thread delivery and "
       "detached callback passed");
  return 0;
}
