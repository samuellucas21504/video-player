#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
  FILE *pFile;
  char szFilename[64]; // Aumentado para evitar buffer overflow
  int y;

  // Criar o nome do arquivo
  snprintf(szFilename, sizeof(szFilename), "frame%d.ppm", iFrame);

  // Abrir o arquivo para escrita
  pFile = fopen(szFilename, "wb");
  if (!pFile) {
    fprintf(stderr, "Failed to open file %s for writing\n", szFilename);
    return;
  }

  // Escrever o cabeçalho do arquivo PPM
  fprintf(pFile, "P6\n%d %d\n255\n", width, height);

  // Escrever os dados RGB dos pixels
  for (y = 0; y < height; y++) {
    fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);
  }

  // Fechar o arquivo
  fclose(pFile);

  printf("Saved frame %d to %s\n", iFrame, szFilename);
}

int main(int argc, char *argv[]) {
  AVFormatContext *pFormatCtx = NULL;

  if (avformat_open_input(&pFormatCtx, argv[1], NULL, 0) != 0) {
    return -1; // Could not open file
  }

  if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
    return -1; // COuld not find stream info
  }

  av_dump_format(pFormatCtx, 0, argv[1], 0);

  int i;
  AVCodecContext *pCodecCtx = NULL;
  AVCodecParameters *pCodecParams = NULL;
  AVStream *videoStream = NULL;

  for (i = 0; i < pFormatCtx->nb_streams; i++) {
    if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStream = pFormatCtx->streams[i];
      break;
    }
  }

  if (!videoStream) {
    return -1;
  }

  pCodecParams = videoStream->codecpar;
  AVCodec *pCodec = avcodec_find_decoder(pCodecParams->codec_id);
  if (!pCodec) {
    fprintf(stderr, "Codec sem suporte!\n");
    return -1;
  }

  pCodecCtx = avcodec_alloc_context3(pCodec);
  if (!pCodecCtx) {
    fprintf(stderr, "Falha ao buscar o contexto do codec\n");
    return -1;
  }

  if (avcodec_parameters_to_context(pCodecCtx, pCodecParams) < 0) {
    fprintf(stderr, "Falha ao copiar os parametros para o contexto do Codec\n");
    avcodec_free_context(&pCodecCtx);
    return -1;
  }

  if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
    fprintf(stderr, "Falha ao abrir o Codec\n");
    avcodec_free_context(&pCodecCtx);
    return -1;
  }

  uint8_t *buffer = NULL;
  int numBytes;

  numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width,
                                      pCodecCtx->height, 1);
  if (numBytes < 0) {
    fprintf(stderr, "Falha ao calcular o tamanho do buffer");
    return -1;
  }

  buffer = (u_int8_t *)av_malloc(numBytes);
  if (!buffer) {
    fprintf(stderr, "Falha ao alocar o buffer\n");
    return -1;
  }

  AVFrame *pFrame = NULL;
  pFrame = av_frame_alloc();

  AVFrame *pFrameRGB = NULL;
  pFrameRGB = av_frame_alloc();
  if (pFrameRGB == NULL) {
    av_free(buffer);
    return -1;
  }

  if (av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer,
                           AV_PIX_FMT_RGB24, pCodecCtx->width,
                           pCodecCtx->height, 1) < 0) {
    fprintf(stderr, "Failed to fill AVFrame arrays\n");
    av_free(buffer);
    av_frame_free(&pFrameRGB);
    return -1;
  }

  struct SwsContext *sws_ctx = NULL;
  AVPacket packet;
  int frameFinished;

  // Inicializar contexto de software scaling (SWS)
  sws_ctx = sws_getContext(
      pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width,
      pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

  i = 0;
  while (av_read_frame(pFormatCtx, &packet) >= 0) {
    if (packet.stream_index == videoStream->index) {
      if (avcodec_send_packet(pCodecCtx, &packet) < 0) {
        fprintf(stderr, "Error sending packet to decoder\n");
        av_packet_unref(&packet);
        continue;
      }

      while (avcodec_receive_frame(pCodecCtx, pFrame) >= 0) {
        // Converter a imagem para RGB
        sws_scale(sws_ctx, (uint8_t const *const *)pFrame->data,
                  pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data,
                  pFrameRGB->linesize);

        // Salvar os primeiros 5 quadros em disco
        if (++i <= 5) {
          SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
        }
      }
    }

    // Liberar o pacote alocado
    av_packet_unref(&packet);
  }

  av_free(buffer);
  av_free(pFrameRGB);
  av_free(pFrame);
  avcodec_close(pCodecCtx);
  avformat_close_input(&pFormatCtx);

  return 0;
}
