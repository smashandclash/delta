// What picolibc's stdio expects underneath it. Nothing on the GBA reads or writes files:
// the core's few error prints go nowhere (the game's log goes to mGBA from main.c).
#include <sys/types.h>
#include <unistd.h>

ssize_t read(int fd, void *buf, size_t n) {
	(void)fd, (void)buf, (void)n;
	return -1;
}

ssize_t write(int fd, const void *buf, size_t n) {
	(void)fd, (void)buf;
	return (ssize_t)n;
}

off_t lseek(int fd, off_t offset, int whence) {
	(void)fd, (void)offset, (void)whence;
	return -1;
}

int close(int fd) {
	(void)fd;
	return -1;
}
