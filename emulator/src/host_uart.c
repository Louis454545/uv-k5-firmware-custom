#define _GNU_SOURCE

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "bsp/dp32g030/dma.h"
#include "driver/uart.h"
#include "host.h"

uint8_t UART_DMA_Buffer[256];

static int uart_fd = -1;
static pthread_t uart_thread;
static bool uart_started;
static char uart_path[256] = "/tmp/uvk5-uart";
static unsigned dma_pos;
static pthread_mutex_t uart_mutex = PTHREAD_MUTEX_INITIALIZER;

static void write_dma_byte(uint8_t byte)
{
	pthread_mutex_lock(&uart_mutex);
	UART_DMA_Buffer[dma_pos] = byte;
	dma_pos = (dma_pos + 1) & 0xff;
	DMA_CH0->ST = dma_pos;
	pthread_mutex_unlock(&uart_mutex);
}

static void *uart_reader(void *arg)
{
	(void)arg;
	while (!gHostShutdown) {
		struct pollfd pfd = {.fd = uart_fd, .events = POLLIN};
		int rc = poll(&pfd, 1, 50);
		if (rc <= 0)
			continue;
		if (pfd.revents & POLLIN) {
			uint8_t buf[128];
			ssize_t n = read(uart_fd, buf, sizeof(buf));
			for (ssize_t i = 0; i < n; i++)
				write_dma_byte(buf[i]);
		}
	}
	return NULL;
}

void UART_Init(void)
{
	if (uart_started)
		return;

	const char *link = getenv("UVK5_UART");
	if (link && *link)
		snprintf(uart_path, sizeof(uart_path), "%s", link);

	int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd >= 0 && grantpt(fd) == 0 && unlockpt(fd) == 0) {
		const char *slave = ptsname(fd);
		if (slave) {
			unlink(uart_path);
			if (symlink(slave, uart_path) != 0)
				snprintf(uart_path, sizeof(uart_path), "%s", slave);
		}
		uart_fd = fd;
		uart_started = true;
		pthread_create(&uart_thread, NULL, uart_reader, NULL);
		pthread_detach(uart_thread);
	}

	memset(UART_DMA_Buffer, 0, sizeof(UART_DMA_Buffer));
	dma_pos = 0;
	DMA_CH0->ST = 0;
}

void HOST_UART_Start(void)
{
	UART_Init();
}

void HOST_UART_Stop(void)
{
	if (uart_fd >= 0) {
		close(uart_fd);
		uart_fd = -1;
	}
}

const char *HOST_UART_Path(void)
{
	return uart_path;
}

void UART_Send(const void *pBuffer, uint32_t Size)
{
	if (uart_fd < 0)
		return;
	const uint8_t *p = (const uint8_t *)pBuffer;
	while (Size) {
		ssize_t n = write(uart_fd, p, Size);
		if (n <= 0)
			break;
		p += n;
		Size -= n;
	}
}

void UART_LogSend(const void *pBuffer, uint32_t Size)
{
	UART_Send(pBuffer, Size);
}
