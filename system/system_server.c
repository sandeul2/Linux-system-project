#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <camera_HAL.h>

pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  system_loop_cond  = PTHREAD_COND_INITIALIZER;
bool            system_loop_exit = false;    ///< true if main loop should exit

static int five_timer = 0;

void signal_exit(void);

static void five_timer_handler() {
    five_timer += 10;
    signal_exit();
    /* printf("five_timer is : %d\n", five_timer); */
}

void set_periodic_timer(long sec_delay, long usec_delay){
   struct itimerval itimer_val = {
       .it_interval = { .tv_sec = sec_delay, .tv_usec = usec_delay },
       .it_value = { .tv_sec = sec_delay, .tv_usec = usec_delay }
    };

   setitimer(ITIMER_REAL, &itimer_val, (struct itimerval*)0);
}

int posix_sleep_ms(unsigned int timeout_ms) {
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}

void *watchdog_thread(void* arg) {
    char *s = arg;

    printf("%s", s);

    while (1) {
        posix_sleep_ms(5000);
    }

    return 0;
}

void *monitor_thread(void* arg) {
    char *s = arg;

    printf("%s", s);

    while (1) {
        posix_sleep_ms(5000);
    }

    return 0;
}

void *disk_service_thread(void* arg) {
    char *s = arg;

    printf("%s", s);

    while (1) {
        posix_sleep_ms(5000);
    }

    return 0;
}

void *camera_service_thread(void* arg) {
    char *s = arg;

    printf("%s", s);

   toy_camera_open();
   toy_camera_take_picture();

    while (1) {
        posix_sleep_ms(5000);
    }

    return 0;
}

void signal_exit(void) {
    /* lab9 : 여기에 구현하세요..  종료 메시지를 보내도록.. */
    pthread_mutex_lock(&system_loop_mutex);
    system_loop_exit = true;
    // broadcast or 시그널 사용
    pthread_cond_broadcast(&system_loop_cond);
    printf("Good bye\n");
    pthread_mutex_unlock(&system_loop_mutex);
}

int system_server() {
    struct itimerspec ts;
    struct sigaction  sa;
    struct sigevent   sev;
    timer_t *tidlist;
    int retcode;
    pthread_t watchdog_thread_tid, monitor_thread_tid, disk_service_thread_tid, camera_service_thread_tid;

    printf("나 system_server 프로세스!\n");

    /* lab6 : 시그널 타이머 핸들러*/
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = five_timer_handler; // five_timer 핸들러
    if (sigaction(SIGALRM, &sa, NULL) == -1) { // signal 함수로도 가능
        printf("sigaction");
        return 0;
    }

    /* lab6 : 5초 타이머를 만들어 봅시다. */
    signal(SIGALRM, five_timer_handler);
    /* lab9 : 10초 타이머로 변경 */
    set_periodic_timer(10, 0);

    /* lab7 : watchdog, monitor, disk_service, camera_service 스레드를 생성한다. */
    retcode = pthread_create(&watchdog_thread_tid, NULL, watchdog_thread, "watchdog thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&monitor_thread_tid, NULL, monitor_thread, "monitor thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&disk_service_thread_tid, NULL, disk_service_thread, "disk service thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&camera_service_thread_tid, NULL, camera_service_thread, "camera service thread\n");
    assert(retcode == 0);

    printf("system init done.  waiting...");
    // lab9 : 여기에 구현하세요... 여기서 cond wait로 대기한다. 10초 후 알람이 울리면 <== system 출력
    // /* 1초 마다 wake-up 한다 */
    pthread_mutex_lock(&system_loop_mutex); // pthread_cond_wait전 lock
    while (system_loop_exit == false) {
        pthread_cond_wait( &system_loop_exit, &system_loop_mutex); // lock 진행시 mutex 동일해야함
        sleep(1);
    }
    pthread_mutex_unlock(&system_loop_mutex);

    while (1) {
        posix_sleep_ms(5000);
    }

    return 0;
}

int create_system_server() {
    pid_t systemPid;
    const char *name = "system_server";

    printf("여기서 시스템 프로세스를 생성합니다.\n");

    /* lab2 : fork 를 이용하세요 */
    switch (systemPid = fork()) {
    case -1:
        printf("fork failed\n");
    case 0:
        /* lab2 : 프로세스 이름 변경 */
        if (prctl(PR_SET_NAME, (unsigned long) name) < 0)
            perror("prctl()");
        system_server(); // system_server 실행
        break;
    default:
        break;
    }

    return 0;
}