// C Program to create stream socket
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define PORT 9000
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

// Global flag to indicate that a signal has been received
volatile sig_atomic_t g_exit_signal_received = 0;

/**
 * Signal handler function to catch SIGINT and SIGTERM.
 */
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_exit_signal_received = 1;
        syslog(LOG_INFO, "Caught signal %s, exiting", (sig == SIGINT) ? "SIGINT" : "SIGTERM");
    }
}

/**
 * Creates a TCP listening socket bound to port 9000.
 * @return The file descriptor of the listening socket on success, -1 on failure.
 */
int create_listening_socket() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int main(int argc, char *argv[]) {
    int listening_socket, client_socket;
    struct sockaddr_in client_address;
    socklen_t client_addr_size = sizeof(client_address);
    char client_ip[INET_ADDRSTRLEN];
    char recv_buffer[BUFFER_SIZE];
    
    char *packet_buffer = NULL;
    size_t packet_buffer_size = 0;
    size_t packet_buffer_len = 0;

    int daemon_mode = 0;

    // Check for the -d argument
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
            break;
        }
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    listening_socket = create_listening_socket();
    if (listening_socket == -1) {
        syslog(LOG_ERR, "Failed to create listening socket. Exiting.");
        closelog();
        return 1;
    }

    // Daemonization logic
    if (daemon_mode) {
        pid_t pid = fork();

        if (pid < 0) {
            // Fork failed
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
            close(listening_socket);
            closelog();
            return 1;
        }
        
        if (pid > 0) {
            // Parent process exits
            syslog(LOG_INFO, "Daemonizing parent process is exiting");
            closelog();
            exit(0);
        }

        // Child process continues as the daemon
        syslog(LOG_INFO, "Running as daemon");

        // Create a new session and become a session leader
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            exit(1);
        }

        // Change the current working directory to the root
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
            exit(1);
        }

        // Close all standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        
        // Optionally, redirect standard file descriptors to /dev/null
      //  open("/dev/null", O_RDWR); // stdin
      //  dup(0); // stdout
      //  dup(0); // stderr
    }
    
    // Register signal handler for SIGINT and SIGTERM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction failed for SIGINT");
        return 1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction failed for SIGTERM");
        return 1;
    }

    syslog(LOG_INFO, "Listening for connections on port %d...", PORT);

    while (!g_exit_signal_received) {
        client_socket = accept(listening_socket, (struct sockaddr *)&client_address, &client_addr_size);
        
        // Check for interrupted system call
        if (client_socket == -1) {
            if (errno == EINTR) {
                // The accept() call was interrupted by a signal, so we check our flag and continue or exit
                continue;
            }
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }

        inet_ntop(AF_INET, &(client_address.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Reset buffer for a new connection
        if (packet_buffer) {
            free(packet_buffer);
            packet_buffer = NULL;
            packet_buffer_size = 0;
            packet_buffer_len = 0;
        }

        ssize_t bytes_received = 0;
        while (!g_exit_signal_received && (bytes_received = recv(client_socket, recv_buffer, BUFFER_SIZE, 0)) > 0) {
            
            if (packet_buffer_len + bytes_received > packet_buffer_size) {
                size_t new_size = packet_buffer_len + bytes_received;
                char *temp = realloc(packet_buffer, new_size);
                if (temp == NULL) {
                    syslog(LOG_ERR, "realloc failed: %s. Discarding over-length packet.", strerror(errno));
                    free(packet_buffer);
                    packet_buffer = NULL;
                    packet_buffer_size = 0;
                    packet_buffer_len = 0;
                    break;
                }
                packet_buffer = temp;
                packet_buffer_size = new_size;
            }

            memcpy(packet_buffer + packet_buffer_len, recv_buffer, bytes_received);
            packet_buffer_len += bytes_received;

            char *newline = (char *)memchr(packet_buffer, '\n', packet_buffer_len);
            if (newline != NULL) {
                size_t packet_len = newline - packet_buffer + 1;

                int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd == -1) {
                    syslog(LOG_ERR, "Failed to open or create file %s: %s", DATA_FILE, strerror(errno));
                    break;
                }

                if (write(fd, packet_buffer, packet_len) == -1) {
                    syslog(LOG_ERR, "Failed to write to file %s: %s", DATA_FILE, strerror(errno));
                }
                close(fd);

                int read_fd = open(DATA_FILE, O_RDONLY);
                if (read_fd == -1) {
                    syslog(LOG_ERR, "Failed to open file %s for reading: %s", DATA_FILE, strerror(errno));
                } else {
                    char file_buffer[BUFFER_SIZE];
                    ssize_t read_bytes = 0;
                    while (!g_exit_signal_received && (read_bytes = read(read_fd, file_buffer, BUFFER_SIZE)) > 0) {
                        ssize_t sent_bytes = 0;
                        while (sent_bytes < read_bytes) {
                            ssize_t current_sent = send(client_socket, file_buffer + sent_bytes, read_bytes - sent_bytes, 0);
                            if (current_sent == -1) {
                                syslog(LOG_ERR, "send failed: %s", strerror(errno));
                                break;
                            }
                            sent_bytes += current_sent;
                        }
                        if (sent_bytes < read_bytes) {
                            break;
                        }
                    }
                    if (read_bytes == -1) {
                         syslog(LOG_ERR, "read failed on file %s: %s", DATA_FILE, strerror(errno));
                    }
                    close(read_fd);
                }
                
                size_t remaining_len = packet_buffer_len - packet_len;
                memmove(packet_buffer, newline + 1, remaining_len);
                packet_buffer_len = remaining_len;
            }
        }

        if (bytes_received == -1 && errno != EINTR) {
            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
        }

        close(client_socket);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }
    
    // --- Graceful Exit and Cleanup ---
    // Close the listening socket
    close(listening_socket);

    // Delete the data file
    if (unlink(DATA_FILE) == -1) {
        syslog(LOG_ERR, "Failed to delete file %s: %s", DATA_FILE, strerror(errno));
    } else {
        syslog(LOG_INFO, "Deleted file %s", DATA_FILE);
    }

    // Free the dynamic buffer if it exists
    if (packet_buffer) {
        free(packet_buffer);
    }

    closelog();
    return 0;
}

