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
#include <pthread.h>
#include <time.h>

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#define PORT 9000
#define BACKLOG 10

#if USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif

//#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

// Global flag to indicate that a signal has been received
volatile sig_atomic_t g_exit_signal_received = 0;

// Mutex for synchronizing file writes
pthread_mutex_t file_mutex;

/**
 * Singly linked list node for managing threads.
 */
struct slist_data_s {
    pthread_t thread;
    int client_socket;
    struct slist_data_s *next;
};
struct slist_data_s *slist_head = NULL;

#if !USE_AESD_CHAR_DEVICE
pthread_t timestamp_thread_id;
#endif

//pthread_t timestamp_thread_id;

/**
 * Data structure to pass to the thread.
 */
struct thread_data {
    int client_socket;
    struct sockaddr_in client_address;
};

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
 * Function to join all active threads.
 */
static void join_all_threads() {
    struct slist_data_s *current = slist_head;
    struct slist_data_s *temp;

    while (current != NULL) {
        pthread_join(current->thread, NULL);
        temp = current;
        current = current->next;
        free(temp);
    }
    slist_head = NULL;
}

/**
 * Creates a TCP listening socket bound to port 9000.
 * @return The file descriptor of the listening socket on success, -1 on failure.
 */
int create_listening_socket() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
#ifdef SO_REUSEPORT
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt SO_REUSEPORT failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
#endif

    memset(&address, 0, sizeof(address));              
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    // Retry bind a few times to tolerate fast restarts
    for (int attempt = 0; attempt < 10; attempt++) {
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == 0) {
            break; // success
        }
        if (errno == EADDRINUSE) {
            syslog(LOG_WARNING, "bind EADDRINUSE, retrying...");
            usleep(200 * 1000); // 200ms backoff
            continue;
        }
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

/**
 * Thread function to handle a single client connection.
 * @param arg A pointer to a struct thread_data containing the client socket.
 * @return NULL on completion.
 */
void *handle_connection(void *arg) {
    struct thread_data *data = (struct thread_data *)arg;
    int client_socket = data->client_socket;
    struct sockaddr_in client_address = data->client_address;
    char client_ip[INET_ADDRSTRLEN];
    char recv_buffer[BUFFER_SIZE];
    
    char *packet_buffer = NULL;
    size_t packet_buffer_size = 0;
    size_t packet_buffer_len = 0;

    inet_ntop(AF_INET, &(client_address.sin_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

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

            // Lock the mutex before writing to the file
            if (pthread_mutex_lock(&file_mutex) != 0) {
                syslog(LOG_ERR, "Failed to lock mutex: %s", strerror(errno));
                break;
            }
            
            int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd == -1) {
                syslog(LOG_ERR, "Failed to open or create file %s: %s", DATA_FILE, strerror(errno));
                pthread_mutex_unlock(&file_mutex); // Unlock on error
                break;
            }

            if (write(fd, packet_buffer, packet_len) == -1) {
                syslog(LOG_ERR, "Failed to write to file %s: %s", DATA_FILE, strerror(errno));
            }
            
            if (fsync(fd) == -1) {
                syslog(LOG_ERR, "fsync failed: %s", strerror(errno));
            }
            close(fd);
            
            // Unlock the mutex after writing
            if (pthread_mutex_unlock(&file_mutex) != 0) {
                syslog(LOG_ERR, "Failed to unlock mutex: %s", strerror(errno));
            }
            
            // Lock the mutex before reading from the file
            if (pthread_mutex_lock(&file_mutex) != 0) {
                syslog(LOG_ERR, "Failed to lock mutex: %s", strerror(errno));
                break;
            }

            int read_fd = open(DATA_FILE, O_RDONLY);
            if (read_fd == -1) {
                syslog(LOG_ERR, "Failed to open file %s for reading: %s", DATA_FILE, strerror(errno));
            } else {
                char file_buffer[BUFFER_SIZE];
                ssize_t read_bytes_from_file = 0;
                
		lseek(read_fd, 0, SEEK_SET);

                while (!g_exit_signal_received && (read_bytes_from_file = read(read_fd, file_buffer, BUFFER_SIZE)) > 0) {
                    ssize_t sent_bytes = 0;
                    while (sent_bytes < read_bytes_from_file) {
                        ssize_t current_sent = send(client_socket, file_buffer + sent_bytes, read_bytes_from_file - sent_bytes, 0);
                        if (current_sent == -1) {
                            syslog(LOG_ERR, "send failed: %s", strerror(errno));
                            break;
                        }
                        sent_bytes += current_sent;
                    }
                    if (sent_bytes < read_bytes_from_file) {
                        break;
                    }
                }
                if (read_bytes_from_file == -1) {
                     syslog(LOG_ERR, "read failed on file %s: %s", DATA_FILE, strerror(errno));
                }
                close(read_fd);
            }
            // Unlock the mutex after reading
            if (pthread_mutex_unlock(&file_mutex) != 0) {
                syslog(LOG_ERR, "Failed to unlock mutex: %s", strerror(errno));
            }

            size_t remaining_len = packet_buffer_len - packet_len;
            memmove(packet_buffer, newline + 1, remaining_len);
            packet_buffer_len = remaining_len;
        }
    }
    
    // Check if connection was closed with data still in buffer
    if (bytes_received == 0 && packet_buffer_len > 0) {
        if (pthread_mutex_lock(&file_mutex) == 0) {
            int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd != -1) {
                if (write(fd, packet_buffer, packet_buffer_len) == -1) {
                     syslog(LOG_ERR, "Failed to write remaining data to file %s: %s", DATA_FILE, strerror(errno));
                }
                
                if (fsync(fd) == -1) {
                    syslog(LOG_ERR, "fsync failed: %s", strerror(errno));
                }
                close(fd);
            } else {
                 syslog(LOG_ERR, "Failed to open file for remaining data %s: %s", DATA_FILE, strerror(errno));
            }
            pthread_mutex_unlock(&file_mutex);
        } else {
            syslog(LOG_ERR, "Failed to lock mutex for final write: %s", strerror(errno));
        }
    } else if (bytes_received == -1 && errno != EINTR) {
        syslog(LOG_ERR, "recv failed: %s", strerror(errno));
    }

    close(client_socket);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);

    // Free the dynamic buffer if it exists
    if (packet_buffer) {
        free(packet_buffer);
    }
    
    // Free the thread data structure
    free(data);
    
    return NULL;
}

/**
 * Thread function to write a timestamp to the data file every 10 seconds.
 * @param arg Not used.
 * @return NULL on completion.
 */
#if !USE_AESD_CHAR_DEVICE 
void *timestamp_thread(void *arg) {
    while (!g_exit_signal_received) {
        // Sleep for 10 seconds or until a signal is received
        sleep(10);
        
        if (g_exit_signal_received) {
            break;
        }

        // Get current time
        time_t raw_time;
        struct tm *info;
        char time_string[256];

        time(&raw_time);
        info = localtime(&raw_time);

        // Format the time string according to RFC 2822
        strftime(time_string, sizeof(time_string), "timestamp:%a, %d %b %Y %T %z\n", info);
        
        // Lock the mutex before writing to the file
        if (pthread_mutex_lock(&file_mutex) != 0) {
            syslog(LOG_ERR, "Failed to lock mutex for timestamp thread: %s", strerror(errno));
            continue;
        }
        
        int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            syslog(LOG_ERR, "Failed to open or create file %s for timestamp: %s", DATA_FILE, strerror(errno));
            pthread_mutex_unlock(&file_mutex);
            continue;
        }
        
        if (write(fd, time_string, strlen(time_string)) == -1) {
            syslog(LOG_ERR, "Failed to write timestamp to file %s: %s", DATA_FILE, strerror(errno));
        }
        
        if (fsync(fd) == -1) {
            syslog(LOG_ERR, "fsync failed for timestamp: %s", strerror(errno));
        }
        close(fd);
        
        // Unlock the mutex
        if (pthread_mutex_unlock(&file_mutex) != 0) {
            syslog(LOG_ERR, "Failed to unlock mutex for timestamp thread: %s", strerror(errno));
        }
    }
    return NULL;
}
#endif

int main(int argc, char *argv[]) {
    int listening_socket;
    struct sockaddr_in client_address;
    socklen_t client_addr_size = sizeof(client_address);
    int daemon_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
            break;
        }
    }

    signal(SIGPIPE, SIG_IGN);

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (daemon_mode) {
        
        pid_t pid = fork();

        if (pid < 0) {
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
            closelog();
            return 1;
        }
        
        if (pid > 0) {
            syslog(LOG_INFO, "Daemonizing parent process is exiting");
            closelog();
            exit(0);
        }
	        
        syslog(LOG_INFO, "Running as daemon");

        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            exit(1);
        }

        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
            exit(1);
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    //sleep(2);
    
    }
    
    listening_socket = create_listening_socket();
    if (listening_socket == -1) {
        syslog(LOG_ERR, "Failed to create listening socket. Exiting.");
        closelog();
        return 1;
    }
    
    if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        syslog(LOG_ERR, "mutex init failed: %s", strerror(errno));
        close(listening_socket);
        closelog();
        return 1;
    }
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction failed for SIGINT");
        pthread_mutex_destroy(&file_mutex);
        return 1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction failed for SIGTERM");
        pthread_mutex_destroy(&file_mutex);
        return 1;
    }

	// At startup, ensure the data file is empty for a clean test run state
	int init_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (init_fd == -1) {
    		syslog(LOG_ERR, "Failed to initialize %s: %s", DATA_FILE, strerror(errno));
	} 	else {
    		close(init_fd);
	}

#if !USE_AESD_CHAR_DEVICE
    if (pthread_create(&timestamp_thread_id, NULL, timestamp_thread, NULL) != 0) {
        syslog(LOG_ERR, "pthread_create for timestamp thread failed: %s", strerror(errno));
        pthread_mutex_destroy(&file_mutex);
        close(listening_socket);
        closelog();
        return 1;
    }
#endif

    //sleep(2);

    syslog(LOG_INFO, "Listening for connections on port %d...", PORT);

    while (!g_exit_signal_received) {
        int client_socket = accept(listening_socket, (struct sockaddr *)&client_address, &client_addr_size);
        
        if (client_socket == -1) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }
        
        struct thread_data *data = malloc(sizeof(struct thread_data));
        if (data == NULL) {
            syslog(LOG_ERR, "malloc failed for thread data: %s", strerror(errno));
            close(client_socket);
            continue;
        }
        data->client_socket = client_socket;
        data->client_address = client_address;
        
        struct slist_data_s *new_node = malloc(sizeof(struct slist_data_s));
        if (new_node == NULL) {
            syslog(LOG_ERR, "malloc failed for list node: %s", strerror(errno));
            free(data);
            close(client_socket);
            continue;
        }
        
        if (pthread_create(&new_node->thread, NULL, handle_connection, data) != 0) {
            syslog(LOG_ERR, "pthread_create failed: %s", strerror(errno));
            free(new_node);
            free(data);
            close(client_socket);
            continue;
        }

        new_node->next = slist_head;
        slist_head = new_node;
        
        struct slist_data_s *current = slist_head;
        struct slist_data_s *prev = NULL;
        while (current != NULL) {
            int join_result = pthread_tryjoin_np(current->thread, NULL);
            if (join_result == 0) {
                if (prev == NULL) {
                    slist_head = current->next;
                } else {
                    prev->next = current->next;
                }
                struct slist_data_s *temp = current;
                current = current->next;
                free(temp);
            } else if (join_result == EBUSY) {
                prev = current;
                current = current->next;
            } else {
                syslog(LOG_ERR, "pthread_tryjoin_np failed: %s", strerror(join_result));
                prev = current;
                current = current->next;
            }
        }
    }
    
    #if !USE_AESD_CHAR_DEVICE
    pthread_join(timestamp_thread_id, NULL);
    #endif
//    pthread_join(timestamp_thread_id, NULL);

    join_all_threads();
    close(listening_socket);
    pthread_mutex_destroy(&file_mutex);
    
    #if !USE_AESD_CHAR_DEVICE    
    if (unlink(DATA_FILE) == -1) {
        syslog(LOG_ERR, "Failed to delete file %s: %s", DATA_FILE, strerror(errno));
    } else {
        syslog(LOG_INFO, "Deleted file %s", DATA_FILE);
    }
    #endif

    closelog();
    return 0;
}
