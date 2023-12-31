#include <assert.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <execinfo.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mqueue.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <execinfo.h>
#include <toy_message.h>
#include <shared_memory.h>

#define TOY_TOK_BUFSIZE 64
#define TOY_TOK_DELIM " \t\r\n\a"
#define TOY_BUFFSIZE 1024

typedef struct _sig_ucontext {
    unsigned long uc_flags;
    struct ucontext *uc_link;
    stack_t uc_stack;
    struct sigcontext uc_mcontext;
    sigset_t uc_sigmask;
} sig_ucontext_t;

static pthread_mutex_t global_message_mutex  = PTHREAD_MUTEX_INITIALIZER;

// lab8 : global_message <~ 모든 문제를 만드는 전역 변수
static char global_message[TOY_BUFFSIZE];

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;
static shm_sensor_t *the_sensor_info = NULL;

void segfault_handler(int sig_num, siginfo_t * info, void * ucontext) {
  void * array[50];
  void * caller_address;
  char ** messages;
  int size, i;
  sig_ucontext_t * uc;

  uc = (sig_ucontext_t *) ucontext;

  /* Get the address at the time the signal was raised */
  caller_address = (void *) uc->uc_mcontext.rip;  // RIP: x86_64 specific     arm_pc: ARM

  fprintf(stderr, "\n");

  if (sig_num == SIGSEGV)
    printf("signal %d (%s), address is %p from %p\n", sig_num, strsignal(sig_num), info->si_addr,
           (void *) caller_address);
  else
    printf("signal %d (%s)\n", sig_num, strsignal(sig_num));

  size = backtrace(array, 50);
  /* overwrite sigaction with caller's address */
  array[1] = caller_address;
  messages = backtrace_symbols(array, size);

  /* skip first stack frame (points here) */
  for (i = 1; i < size && messages != NULL; ++i) {
    printf("[bt]: (%d) %s\n", i, messages[i]);
  }

  free(messages);

  exit(EXIT_FAILURE);
}

/*
 *  lab7 : sensor thread
 */
void *sensor_thread(void* arg) {
    int mqretcode;
    char *s = arg;
    toy_msg_t msg;
    // 여기 추가: 공유메모리 키
    int shmid = toy_shm_get_keyid(SHM_KEY_SENSOR);

    printf("%s", s);

    while (1) {
        posix_sleep_ms(5000);
        // lab13 : 여기에 구현해 주세요.
        // 현재 고도/온도/기압 정보를  SYS V shared memory에 저장 후
        // monitor thread에 메시지 전송한다.
        if (the_sensor_info != NULL) {
            the_sensor_info->temp = 35;
            the_sensor_info->press = 55;
            the_sensor_info->humidity = 80;
        }
        
        msg.msg_type = 1;
        msg.param1 = shmid;
        msg.param2 = 0;
        mqretcode = mq_send(monitor_queue, (char *)&msg, sizeof(msg), 0);
        assert(mqretcode == 0);
    }

    return 0;
}

/*
 *  lab7 : command thread
 */

int toy_send(char **args);
int toy_mutex(char **args);
int toy_shell(char **args);
int toy_message_queue(char **args);
int toy_exit(char **args);

char *builtin_str[] = {
    "send",
    "mu",
    "sh",
    "mq",
    "exit"
};

int (*builtin_func[]) (char **) = {
    &toy_send,
    &toy_mutex,
    &toy_shell,
    &toy_message_queue,
    &toy_exit
};

int toy_num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
}

int toy_send(char **args) {
    printf("send message: %s\n", args[1]);
    return 1;
}

int toy_mutex(char **args) {
    if (args[1] == NULL) {
        return 1;
    }

    printf("save message: %s\n", args[1]);
    // lab8 : 여기서 뮤텍스
    pthread_mutex_lock(&global_message_mutex);
    strcpy(global_message, args[1]);
    pthread_mutex_unlock(&global_message_mutex);

    return 1;
}

int toy_message_queue(char **args) {
    int mqretcode;
    toy_msg_t msg;

    if (args[1] == NULL || args[2] == NULL) {
        return 1;
    }

    if (!strcmp(args[1], "camera")) {
        msg.msg_type = atoi(args[2]);
        msg.param1 = 0;
        msg.param2 = 0;
        mqretcode = mq_send(camera_queue, (char *)&msg, sizeof(msg), 0);
        assert(mqretcode == 0);
    }

    return 1;
}

int toy_exit(char **args) {
    return 0;
}

int toy_shell(char **args) {
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) {
        if (execvp(args[0], args) == -1) {
            perror("toy");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("toy");
    } else
{
        do
        {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

int toy_execute(char **args) {
    int i;

    if (args[0] == NULL) {
        return 1;
    }

    for (i = 0; i < toy_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    return 1;
}

char *toy_read_line(void)
{
    char *line = NULL;
    ssize_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            exit(EXIT_SUCCESS);
        } else {
            perror(": getline\n");
            exit(EXIT_FAILURE);
        }
    }
    return line;
}

char **toy_split_line(char *line)
{
    int bufsize = TOY_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token, **tokens_backup;

    if (!tokens) {
        fprintf(stderr, "toy: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOY_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += TOY_TOK_BUFSIZE;
            tokens_backup = tokens;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                free(tokens_backup);
                fprintf(stderr, "toy: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOY_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

void toy_loop(void) {
    char *line;
    char **args;
    int status;

    do {
        // lab8 : 여기는 그냥 중간에 "TOY>"가 출력되는거 보기 싫어서.. 뮤텍스
        pthread_mutex_lock(&global_message_mutex);
        printf("TOY>");
        pthread_mutex_unlock(&global_message_mutex);

        line = toy_read_line();
        args = toy_split_line(line);
        status = toy_execute(args);

        free(line);
        free(args);
    } while (status);
}

void *command_thread(void* arg) {
    char *s = arg;

    printf("%s", s);

    toy_loop();

    return 0;
}

// lab 9: 토이 생산자 소비자 실습
// 임시로 추가
// #define MAX 30
// #define NUMTHREAD 3 /* number of threads */

// char buffer[TOY_BUFFSIZE];
// int read_count = 0, write_count = 0;
// int buflen;
// pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
// pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
// int thread_id[NUMTHREAD] = {0, 1, 2};
// int producer_count = 0, consumer_count = 0;

// void *toy_consumer(int *id)
// {
//     pthread_mutex_lock(&count_mutex);
//     while (consumer_count < MAX) {
//         pthread_cond_wait(&empty, &count_mutex);
//         // 큐에서 하나 꺼낸다.
//         printf("                           소비자[%d]: %c\n", *id, buffer[read_count]);
//         read_count = (read_count + 1) % TOY_BUFFSIZE;
//         fflush(stdout);
//         consumer_count++;
//     }
//     pthread_mutex_unlock(&count_mutex);
// }

// void *toy_producer(int *id)
// {
//     while (producer_count < MAX) {
//         pthread_mutex_lock(&count_mutex);
//         strcpy(buffer, "");
//         buffer[write_count] = global_message[write_count % buflen];
//         // 큐에 추가한다.
//         printf("%d - 생산자[%d]: %c \n", producer_count, *id, buffer[write_count]);
//         fflush(stdout);
//         write_count = (write_count + 1) % TOY_BUFFSIZE;
//         producer_count++;
//         pthread_cond_signal(&empty);
//         pthread_mutex_unlock(&count_mutex);
//         sleep(rand() % 3);
//     }
// }

int input(){
    printf("나 input 프로세스!\n");

    int retcode;
    struct sigaction sa;
    pthread_t command_thread_tid, sensor_thread_tid;
    int i;

    // // lab9 : 추가
    // int i;
    // pthread_t thread[NUMTHREAD];

    memset(&sa, 0, sizeof(sigaction)); // sa 초기화
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = segfault_handler;

    // seg falut 핸들러
     sigaction(SIGSEGV, &sa, NULL); /* ignore whether it works or not */

    /* lab13 : 센서 정보를 공유하기 위한, 시스템 V 공유 메모리를 생성한다 */
    // 여기에 구현해주세요....
    the_sensor_info=(shm_sensor_t *) toy_shm_create(SHM_KEY_SENSOR,sizeof(shm_sensor_t));
    if(the_sensor_info==(void*)-1) {
        the_sensor_info=NULL;
		printf("Error in shm_create SHmI%d SHM_KEY_SENSOR\n",SHM_KEY_SENSOR);
    }	

    /* lab11 : 메시지 큐를 오픈 한다. */
    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);
    camera_queue = mq_open("/camera_queue", O_RDWR);
    assert(camera_queue != -1);

    /* lab7 : 여기서 스레드를 생성한다. */
    retcode = pthread_create(&command_thread_tid, NULL, command_thread, "command thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&sensor_thread_tid, NULL, sensor_thread, "sensor thread\n");
    assert(retcode == 0);

    /* lab9 : 생산자 소비자 실습 */
    // pthread_mutex_lock(&global_message_mutex);
    // strcpy(global_message, "hello world!");
    // buflen = strlen(global_message);
    // pthread_mutex_unlock(&global_message_mutex);
    // pthread_create(&thread[0], NULL, (void *)toy_consumer, &thread_id[0]);
    // pthread_create(&thread[1], NULL, (void *)toy_producer, &thread_id[1]);
    // pthread_create(&thread[2], NULL, (void *)toy_producer, &thread_id[2]);

    // for (i = 0; i < NUMTHREAD; i++) {
    //     pthread_join(thread[i], NULL);
    // }

    while (1) {
        sleep(1);
    }

    return 0;
}

int create_input()
{
    pid_t systemPid;
    const char *name = "input";

    printf("여기서 input 프로세스를 생성합니다.\n");

    /* fork 를 이용하세요 */
    switch (systemPid = fork()) {
    case -1:
        printf("fork failed\n");
    case 0:
        /* 프로세스 이름 변경 */
        if (prctl(PR_SET_NAME, (unsigned long) name) < 0)
            perror("prctl()");
        input();
        break;
    default:
        break;
    }

    return 0;
}