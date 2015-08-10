#include <signal.h>
#include <unistd.h>

int main() {
    signal(SIGCHLD, SIG_IGN);

    while (1) {
        pause();
    }
}
