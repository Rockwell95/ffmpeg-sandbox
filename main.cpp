#include <iostream>
#include <fstream>
#include <array>
#include "Config.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}

using std::cout;
using std::endl;
using std::ifstream;

int main(int argc, char *argv[]) {
    AVFormatContext *pFormatCtx;

    cout << "FFMPEG Sandbox Version " << FFMPEGSandbox_VERSION_MAJOR << "." << FFMPEGSandbox_VERSION_MINOR << endl;
    cout << "Hello World!" << endl;

#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();

    ifstream inFile;
    inFile.open("/home/dmancini/Streams/t11.ts");

    avformat_open_input(&pFormatCtx, "/home/dmancini/Streams/t11.ts", nullptr, nullptr);

    const AVStream *streams = *(pFormatCtx->streams);
    char **buffer = new char *[2]();
    for (int i = 0; i < 2; i++) {
        buffer[i] = new char[256]();
    }
    av_dict_get_string(streams->metadata, buffer, '=', ';');

    cout << *buffer << endl;

    for (int i = 0; i < 2; i++) {
        delete buffer[i];
        buffer[i] = nullptr;
    }
    delete[] buffer;
    buffer = nullptr;

    return EXIT_SUCCESS;
}