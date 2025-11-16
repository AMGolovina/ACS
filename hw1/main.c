#include <unistd.h>     
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define BUF_SIZE 32     // размер буфера 32 байта

int main(int argc, char *argv[])
{
    int fd_src;
    int fd_dst;
    ssize_t nread;
    char buf[BUF_SIZE];
    struct stat st;

    const char *src_path = argv[1]; // имя исходного файла
    const char *dst_path = argv[2]; // имя целевого

    fd_src = open(src_path, O_RDONLY); // исходный файл только для чтения

    fstat(fd_src, &st); // получаем права доступа

    // открываем/создаём целевой файл для записи, очищаем старое содержимое
    fd_dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    // читаем из исходного файла маленькими порциями до 32 байт
    while ((nread = read(fd_src, buf, sizeof(buf))) > 0) {
        ssize_t total_written = 0;

        // записываем прочитанные байты в целевой файл, пока не запишем все nread байт
        while (total_written < nread) {
            ssize_t nw = write(fd_dst,
                               buf + total_written,
                               (size_t)(nread - total_written));
            total_written += nw;
        }
    }

    fchmod(fd_dst, st.st_mode); // переносим права доступа

    fchown(fd_dst, st.st_uid, st.st_gid); // переносим владельца и группу (как у исходного файла)

    // закрываем оба файловых дескриптора
    close(fd_src);
    close(fd_dst);

    return 0;
}

