#include "Config.h"
#include <array>
#include <cstring>
#include <execinfo.h>
#include <fstream>
#include <iostream>
#include <signal.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
}

using std::cout;
using std::endl;
using std::ifstream;

void handler(int sig) {
  void *array[10];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 10);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}

int main(int argc, char *argv[]) {
  AVFormatContext *pFormatCtx = nullptr;

  signal(SIGBUS, handler);

  cout << "FFMPEG Sandbox Version " << FFMPEGSandbox_VERSION_MAJOR << "." << FFMPEGSandbox_VERSION_MINOR << endl;

#if CONFIG_AVDEVICE
  avdevice_register_all();
#endif
  avformat_network_init();
  cout << argv[1] << endl;

  avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr);

  const AVStream *streams = *(pFormatCtx->streams);
  const AVProgram *programs = *(pFormatCtx->programs);
  char **buffer = new char *[2]();
  for (int i = 0; i < 2; i++) {
    buffer[i] = new char[256]();
  }
  av_dict_get_string(programs->metadata, buffer, '=', ';');

  cout << *buffer << endl;

  for (int i = 0; i < 2; i++) {
    delete buffer[i];
    buffer[i] = nullptr;
  }
  delete[] buffer;
  buffer = nullptr;

  return EXIT_SUCCESS;
}