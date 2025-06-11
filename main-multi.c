#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define stdHandle GetStdHandle(STD_OUTPUT_HANDLE)
#define isPressed(K) (GetAsyncKeyState(K)&0x8000)

#define DLL_NAME "ws2_32.dll"
#define RESOLVE(fn)  (fn##_t)GetProcAddress(hMod, #fn)
#define LOCAL_STORAGE_FILE "local_storage.json"

#undef  htons     /* 매크로 → undef 해서 심볼 참조 제거 */
#undef  htonl

static HMODULE hMod;
typedef int  (WINAPI *WSAStartup_t)(WORD, LPWSADATA);
typedef int  (WINAPI *WSACleanup_t)(void);
typedef SOCKET (WINAPI *socket_t)(int,int,int);
typedef int  (WINAPI *connect_t)(SOCKET,const struct sockaddr*,int);
typedef int  (WINAPI *send_t)(SOCKET,const char*,int,int);
typedef int  (WINAPI *recv_t)(SOCKET,char*,int,int);
typedef int  (WINAPI *shutdown_t)(SOCKET,int);
typedef int  (WINAPI *closesocket_t)(SOCKET);
typedef int (WINAPI *WSAGetLastError_t)(void);   // 추가

static WSAStartup_t   pWSAStartup;
static WSACleanup_t   pWSACleanup;
static socket_t       psocket;
static connect_t      pconnect;
static send_t         psend;
static recv_t         precv;
static shutdown_t     pshutdown;
static closesocket_t  pclosesocket;
static WSAGetLastError_t pWSAGetLastError;

typedef u_short (WINAPI *htons_t)(u_short);
typedef u_long  (WINAPI *htonl_t)(u_long);
static htons_t phtons;
static htonl_t phtonl;

/* ─── network byte-order 헬퍼 ──────────────────────────────── */
static unsigned short htons16(unsigned short v)
{ return (v >> 8) | (v << 8); }               /* 16-bit swap  */

static unsigned long  htonl32(unsigned long  v)
{ return  (v >> 24) |
         ((v >>  8) & 0x0000FF00) |
         ((v <<  8) & 0x00FF0000) |
          (v << 24); }                        /* 32-bit swap  */

/// 소켓 통신을 위한 라이브러리가 필요한데 컴파일 옵션을 넣기에는 장치마다 다 세팅을 해줘야 해서 DLL 파일을 직접 다이나믹하게 로딩하기 위해 넣었어요 선생님 진짜 힘들었습니다 ㅠㅠㅠㅠㅠㅠㅠㅠㅠㅠㅠ
int winsock_dynload(void)
{
    hMod = LoadLibraryA(DLL_NAME);                         /* DLL 적재 */
    if (!hMod) { fprintf(stderr,"LoadLibrary failed\n"); return 0; }

    pWSAStartup   = RESOLVE(WSAStartup);
    pWSACleanup   = RESOLVE(WSACleanup);
    psocket       = RESOLVE(socket);
    pconnect      = RESOLVE(connect);
    psend         = RESOLVE(send);
    precv         = RESOLVE(recv);
    pshutdown     = RESOLVE(shutdown);
    pclosesocket  = RESOLVE(closesocket);
    pWSAGetLastError = RESOLVE(WSAGetLastError);
    phtons  = RESOLVE(htons);
    phtonl  = RESOLVE(htonl);
    if (!phtons || !phtonl) { fputs("htons/htonl load fail\n", stderr); return 0; }

    if (!pWSAStartup||!psocket||!pconnect) {
        fprintf(stderr,"GetProcAddress failed\n");
        FreeLibrary(hMod);
        return 0;
    }

    WSADATA wsa;
    if (pWSAStartup(MAKEWORD(2,2), &wsa)!=0){
        fprintf(stderr,"WSAStartup err\n");
        FreeLibrary(hMod);
        return 0;
    }
    return 1;
}

void winsock_unload(void)
{
    if (pWSACleanup) pWSACleanup();
    if (hMod) FreeLibrary(hMod);
}



// 위치를 구조체로 표현
typedef struct Location
{
    int x;
    int y;
}Location;

typedef struct {
    int buf[4];
    int head, tail;
} DirQ;

void dq_init(DirQ* q){ q->head = q->tail = 0; }
bool dq_empty(DirQ* q){ return q->head == q->tail; }
bool dq_full(DirQ* q){ return ((q->tail+1)&3) == q->head; }
void dq_push(DirQ* q, int d){
    if (dq_full(q)) return;
    q->buf[q->tail] = d;
    q->tail = (q->tail + 1) & 3;
}
int dq_pop(DirQ* q){
    int d = q->buf[q->head];
    q->head = (q->head + 1) & 3;
    return d;
}

// 색 & 방향을 미리 const값으로 정의
const int Right = 0;
const int Left = 1;
const int Up = 2;
const int Down = 3;

const int Black = 0;
const int Blue = 1;
const int Green = 2;
const int BlueGreen = 3;
const int Red = 4;
const int Purple = 5;
const int Yellow = 6;
const int White = 7;
const int Gray = 8;
const int LightBlue = 9;
const int LightGreen = 10;
const int LightBlueGreen = 11;
const int LightRed = 12;
const int LightPurple = 13;
const int LightYellow = 14;
const int LightWhite = 15;

unsigned long dw;

const Location dir[4] = { {1, 0}, {-1, 0}, {0, -1}, {0, 1} };

/// 위치가 범위 밖인지 아닌지 검사하는 함수
bool isOutOfRange(Location here) {
    return (here.x <= 0 || here.y <= 0 || here.x > 39 || here.y > 39);
}

/// 콘솔 커서 위치 옮기기
void gotoxy(SHORT x, SHORT y)
{
    COORD pos = { x, y };
    SetConsoleCursorPosition(stdHandle, pos);
}

/// @brief 맵 기준으로 커서 옮기기
void gotoMapLoc(Location L) {
    COORD pos = { L.x*2-1, L.y };
    SetConsoleCursorPosition(stdHandle, pos);
}

/// @brief 맵 기준으로 커서 옮기기
void gotoMapXY(SHORT x, SHORT y) {
    COORD pos = { x*2-1, y };
    SetConsoleCursorPosition(stdHandle, pos);
}

/// 현재 위치에서 바라보고 있는 방향으로 1칸 이동한 위치를 반환
Location DIRtoLOC(Location here, int facing)
{
    Location ret = { here.x + dir[facing].x, here.y + dir[facing].y };
    return ret;
}

/// 현재 보고 있는 방향의 반대 방향을 반환
int ReverseDirection(int d)
{
    switch (d)
    {
        case 0: // Right
            return Left;
        case 1: // Left
            return Right;
        case 2: // Up
            return Down;
        case 3: // Down
            return Up;
    }
}

/// from ~ to 사이 난수를 반환
int getRandomNumber(int from, int to) {
    return (rand()+time(0)) % (to - from + 1) + from;
}

/// 1 ~ 맵 크기 사이 난수 반환
int getRandom() { return getRandomNumber(1, 39); }

/// 기본 틀 렌더링
void renderBorder()
{
    Sleep(10);
    gotoxy(0, 0); printf("┌");
    gotoxy(40*2-1, 0); printf("┐");
    gotoxy(40*2-1, 40); printf("┘");
    gotoxy(0, 40); printf("└");

    for (int i = 1; i < 40; i++)
    {
        gotoxy(i*2-1, 0); printf("──");
        gotoxy(i*2-1, 40); printf("──");
        gotoxy(0, i); printf("│");
        gotoxy(40*2-1, i); printf("│");
    }
    puts("");
}



void setColor(int col)
{
    SetConsoleTextAttribute(stdHandle, col);
}

/// 스네이크의 기본 위치에 렌더링
void renderFirstSnake()
{
    gotoMapXY(10, 20); setColor(Green); printf("██");
    for (int i = 6; i <= 9; i++)
    {
        setColor(LightGreen);
        gotoMapXY(i, 20);
        printf("██");
    }
    setColor(White);
}

typedef struct Node {
    Location data;
    struct Node* prev;
    struct Node* next;
}Node;

// Snake 구조체 구현을 위한 덱 자료구조 구현
typedef struct Deque {
    Node* head;
    Node* tail;
    int size;
    void (*push_front)(struct Deque* _d, Location data);
    void (*push_back)(struct Deque* _d, Location data);
    Location (*front)(struct Deque* _d);
    Location (*back)(struct Deque* _d);
    void (*pop_front)(struct Deque* _d);
    void (*pop_back)(struct Deque* _d);
}Deque;
void _push_front(Deque* d, Location data)
{
    Node* n = malloc(sizeof *n);
    n->data = data;
    n->prev = NULL;
    n->next = d->head;

    if (d->head)
        d->head->prev = n;
    else
        d->tail = n;

    d->head = n;
    d->size++;
}
void _push_back(Deque* d, Location data)
{
    Node* n = malloc(sizeof *n);
    n->data = data;
    n->next = NULL;
    n->prev = d->tail;

    if (d->tail)
        d->tail->next = n;
    else
        d->head = n;

    d->tail = n;
    d->size++;
}
Location _front(Deque* _d) { return _d->head->data; }
Location _back(Deque* _d) { return _d->tail->data; }
void _pop_front(Deque* _d) {
    if (_d->size <= 0) return;
    Node* newHead = _d->head->next;
    free(_d->head);
    _d->head = newHead;
    _d->size--;
}
void _pop_back(Deque* _d) {
    if (_d->size <= 0) return;
    Node* newTail = _d->tail->prev;
    free(_d->tail);
    _d->tail = newTail;
    _d->size--;
}
Deque* newDeque() {
    Deque* D = (Deque*)malloc(sizeof(Deque));

    D->size = 0;
    D->head = NULL;
    D->tail = NULL;
    D->push_front = _push_front;
    D->push_back = _push_back;
    D->front = _front;
    D->back = _back;
    D->pop_back = _pop_back;
    D->pop_front = _pop_front;

    return D;
}

typedef struct StrNode {
    char* data;
    struct StrNode* prev;
    struct StrNode* next;
}StrNode;

// Snake 구조체 구현을 위한 덱 자료구조 구현
typedef struct StrDeque {
    StrNode* head;
    StrNode* tail;
    int size;
    void (*push_front)(struct StrDeque* _d, char* data);
    void (*push_back)(struct StrDeque* _d, char* data);
    char* (*front)(struct StrDeque* _d);
    char* (*back)(struct StrDeque* _d);
    void (*pop_front)(struct StrDeque* _d);
    void (*pop_back)(struct StrDeque* _d);

    int (*find)(struct StrDeque* _d,char* E);
    char* (*at)(struct StrDeque* _d,int index);
    void (*change)(struct StrDeque* _d,int index,char* E);
}StrDeque;

int _2find(StrDeque* _d, char* E) {
    int ret = -1;
    StrNode* cur = _d->head;
    while (cur != NULL) {
        ret++;
        if (strcmp(cur->data, E) == 0) return ret;
        cur = cur->next;
    }
    return -1;
}
char* _2at(StrDeque* _d, int index) {
    StrNode* cur = _d->head;
    for (int i = 0; i < index; i++) {
        cur = cur->next;
    }
    return cur->data;
}
void* _2change(StrDeque* _d, int index, char* E) {
    StrNode* cur = _d->head;
    for (int i = 0; i < index; i++) {
        cur = cur->next;
    }
    cur->data = E;
}
void _2push_front(StrDeque* d, char* data)
{
    StrNode* n = malloc(sizeof *n);
    n->data = data;
    n->prev = NULL;
    n->next = d->head;

    if (d->head)
        d->head->prev = n;
    else
        d->tail = n;

    d->head = n;
    d->size++;
}
void _2push_back(StrDeque* d, char* data)
{
    StrNode* n = malloc(sizeof *n);
    n->data = data;
    n->next = NULL;
    n->prev = d->tail;

    if (d->tail)
        d->tail->next = n;
    else
        d->head = n;

    d->tail = n;
    d->size++;
}
char* _2front(StrDeque* _d) { return _d->head->data; }
char* _2back(StrDeque* _d) { return _d->tail->data; }
void _2pop_front(StrDeque* _d) {
    if (_d->size <= 0) return;
    StrNode* newHead = _d->head->next;
    free(_d->head);
    _d->head = newHead;
    _d->size--;
}
void _2pop_back(Deque* _d) {
    if (_d->size <= 0) return;
    Node* newTail = _d->tail->prev;
    free(_d->tail);
    _d->tail = newTail;
    _d->size--;
}
StrDeque* newStrDeque() {
    StrDeque* D = (StrDeque*)malloc(sizeof(StrDeque));

    D->size = 0;
    D->head = NULL;
    D->tail = NULL;
    D->push_front = _2push_front;
    D->push_back = _2push_back;
    D->front = _2front;
    D->back = _2back;
    D->pop_back = _2pop_back;
    D->pop_front = _2pop_front;
    D->find = _2find;
    D->at = _2at;
    D->change = _2change;

    return D;
}

typedef struct Snake {
    bool isGameOvered;
    Location here;
    int facing, score, speed;
    int ms_per_block;
    Deque block;
    DirQ inputQ;
    char map[40][40];
    void (*addScore)(struct Snake* S, int sc);
    void (*addSpeed)(struct Snake* S, int sp);
    void (*generateKillTriangle)(struct Snake* S);
    void (*setFacing)(struct Snake* S, int facing);
    bool (*Move)(struct Snake* S);
    void (*generateApple)(struct Snake* S);
}Snake;

void _addScore(Snake* S, int sc) {
    S->score += sc;
    gotoxy(120, 4);
    setColor(LightGreen);
    printf("현재 스코어: %d점", S->score);
    setColor(White);
}

void _addSpeed(Snake* S, int sp) {
    if (S->speed - sp < 10) return;
    setColor(LightBlue);
    S->speed -= sp;
    S->ms_per_block = S->speed;
    gotoxy(120, 5);
    printf("현재 속도: %d        ", S->speed);
    setColor(White);
}

void _generateKillTriangle(Snake* S) {
    Location R;
    do {
        R.x = getRandomNumber(1, 39);
        R.y = getRandomNumber(1, 39);
    } while (S->map[R.y][R.x] != '.');
    S->map[R.y][R.x] = 'T';

    gotoMapLoc(R);
    setColor(LightRed);
    printf("▲");
    setColor(White);
}

void _setFacing(Snake* S, int newFacing) {
    if (ReverseDirection(S->facing) == newFacing) return;
    S->facing = newFacing;
}

/// @brief 스네이크의 틱 당 움직임을 처리하는 함수
/// @return 스네이크가 정상적으로 움직임에 성공했으면 true, 벽 등에 충돌한 경우 false
bool _Move(Snake* S) {
    Location to = DIRtoLOC(S->here, S->facing);

    Location here = S->here;
    Deque* block = &(S->block);
    setColor(LightGreen);

    // 벽에 충돌하는 경우
    if (isOutOfRange(to)) return true;

    // 만약 가려는 곳에 사과가 있다면
    if (S->map[to.y][to.x] == 'A') {
        block->push_front(block, to);

        S->map[here.y][here.x] = 'o';
        S->map[to.y][to.x] = 'O';

        // 사과를 먹었으므로 lazy하게 화면 업데이트
        gotoMapLoc(here);
        printf("██");


        gotoMapLoc(to);
        printf("██");
        setColor(LightGreen);

        S->generateApple(S);
        S->generateKillTriangle(S);
        S->addScore(S, 100);
        S->addSpeed(S, 10);
    } else if (S->map[to.y][to.x] == '.') {
        Location removal = block->back(block);
        block->pop_back(block);
        block->push_front(block, to);

        S->map[removal.y][removal.x] = '.';
        S->map[here.y][here.x] = 'o';
        S->map[to.y][to.x] = 'O';

        gotoMapLoc(removal);
        printf("  ");

        gotoMapLoc(here);
        printf("██");


        gotoMapLoc(to);
        printf("██");
        setColor(LightGreen);
    } else {
        return true;
    }
    S->here = to;
    S->addScore(S, 1);
    setColor(White);
    return false;
}

void _generateApple(Snake* S) {
    Location R;
    do {
        R.x = getRandomNumber(1, 39);
        R.y = getRandomNumber(1, 39);
    } while (S->map[R.y][R.x] != '.');
    S->map[R.y][R.x] = 'A';
    gotoMapLoc(R);
    setColor(Yellow);
    printf("★");
    setColor(White);
}

Snake* newSnake() {
    Snake* newS = (Snake*)malloc(sizeof(Snake));
    newS->block = *(newDeque());
    Deque* block = &(newS->block);
    newS->isGameOvered = false;
    newS->score = 0;
    newS->speed = 150;

    for (int i = 1; i < 40; i++) {
        for (int j = 1; j < 40; j++) {
            newS->map[i][j] = '.';
        }
    }

    newS->here.x = 10;
    newS->here.y = 20;
    for (int x = 10; x >= 6; x--) {
        Location L = {x, 20};
        block->push_back(block, L);
        newS->map[20][x] = 'o';
    }
    newS->map[20][10] = 'O';

    newS->addScore = _addScore;
    newS->addSpeed = _addSpeed;
    newS->setFacing = _setFacing;
    newS->Move = _Move;
    newS->generateApple = _generateApple;
    newS->generateKillTriangle = _generateKillTriangle;
    newS->facing = Right;
    dq_init(&newS->inputQ);
    return newS;
}

Snake* player;
void lobby();
DWORD WINAPI moveSnakeThread(LPVOID lpParam){
    player->generateApple(player);
    while (!player->isGameOvered){
        if (!dq_empty(&player->inputQ)){
            int nextDir = dq_pop(&player->inputQ);
            player->setFacing(player, nextDir);
        }
        player->isGameOvered = player->Move(player);
        Sleep(player->ms_per_block);
    }
    return 0;
}
DWORD WINAPI rotateSnakeThread(LPVOID lpParam){
    bool prev[4] = {0};
    while (!player->isGameOvered){
        const int vk[4] = {VK_RIGHT, VK_LEFT, VK_UP, VK_DOWN};
        for (int i=0;i<4;i++){
            bool now = GetAsyncKeyState(vk[i]) & 0x8000;
            if (now && !prev[i]){
                if (ReverseDirection(player->facing) != i)
                    dq_push(&player->inputQ, i);
            }
            prev[i] = now;
        }
    }
    return 0;
}

void GameOver() {
    COORD T= {0, 0};
    FillConsoleOutputCharacter(stdHandle, ' ', 300 * 300, T, &dw);
    gotoxy(0, 0);
    setColor(Red);
    puts(" _______  _______  __   __  _______    _______  __   __  _______  ______   ");
    puts("|       ||   _   ||  |_|  ||       |  |       ||  | |  ||       ||    _ |  ");
    puts("|    ___||  |_|  ||       ||    ___|  |   _   ||  |_|  ||    ___||   | ||  ");
    puts("|   | __ |       ||       ||   |___   |  | |  ||       ||   |___ |   |_||_ ");
    puts("|   ||  ||       ||       ||    ___|  |  |_|  ||       ||    ___||    __  |");
    puts("|   |_| ||   _   || ||_|| ||   |___   |       | |     | |   |___ |   |  | |");
    puts("|_______||__| |__||_|   |_||_______|  |_______|  |___|  |_______||___|  |_|");

    setColor(White);
    gotoxy(60, 15);
    printf("최종 스코어: ");
    int sc = player->score;
    int WaitingTime = 200;
    for (int i = 0; i <= sc; i++)
    {
        gotoxy(73, 15);
        printf("        ");
        gotoxy(73, 15);
        printf("%d", i);
        WaitingTime -= WaitingTime * 0.1;
        Sleep(WaitingTime);
    }

    gotoxy(0, 9);
    if (sc < 300)
    {
        setColor(Red);
        puts("         _______ ");
        puts("        |       |");
        puts("        |    ___|");
        puts("        |   |___ ");
        puts("        |    ___|");
        puts("        |   |    ");
        puts("        |___|    ");
    }
    else if (sc < 700)
    {
        setColor(Blue);
        puts("         _______ ");
        puts("        |       |");
        puts("        |    ___|");
        puts("        |   |    ");
        puts("        |   |___ ");
        puts("        |       |");
        puts("        |_______|");
    }
    else if (sc < 2000)
    {
        setColor(BlueGreen);
        puts("         _______ ");
        puts("        |  _    |");
        puts("        | |_|   |");
        puts("        |      _|");
        puts("        |  _  |_ ");
        puts("        | |_|   |");
        puts("        |_______|");
    }
    else if (sc < 3000)
    {
        setColor(Green);
        puts("         _______ ");
        puts("        |   _   |");
        puts("        |  | |  |");
        puts("        |  |_|  |");
        puts("        |       |");
        puts("        |   _   |");
        puts("        |__| |__|");
    }
    else if (sc < 5000)
    {
        setColor(LightGreen);
        puts("         _______    _    ");
        puts("        |   _   | _| |_  ");
        puts("        |  | |  ||_   _| ");
        puts("        |  |_|  |  |_|   ");
        puts("        |       |");
        puts("        |   _   |");
        puts("        |__| |__|");
    }
    else if (sc < 10000)
    {
        setColor(Yellow);
        puts("         _______ ");
        puts("        |       |");
        puts("        |  _____|");
        puts("        | |_____ ");
        puts("        |_____  |");
        puts("         _____| |");
        puts("        |_______|");
    }
    else
    {
        setColor(LightYellow);
        puts("         _______    _    ");
        puts("        |       | _| |_  ");
        puts("        |  _____||_   _| ");
        puts("        | |_____   |_|    ");
        puts("        |_____  |");
        puts("         _____| |");
        puts("        |_______|");
    }
    setColor(White);


    gotoxy(0, 17);
    system("pause");
    lobby();
}
void clearScreen() {
    COORD T = {0, 0};
    FillConsoleOutputCharacter(stdHandle, ' ', 300 * 300, T, &dw);
    Sleep(10);
}


typedef struct JSON {
    StrDeque* properties;
    StrDeque* values;
    void (*set)(struct JSON* J, char* _property, char* value);
    char* (*get)(struct JSON* J, char* _property);
    char* (*toString)(struct JSON* J);
    void (*load)(struct JSON* J, char* S);
}JSON;

void _jload(JSON* J, char* S) {
    J->properties = newStrDeque();
    J->values = newStrDeque();

    char *p = S;
    while (*p && *p != '{') p++;
    if (!*p) return;
    p++;

    while (*p) {
        while (isspace((unsigned char)*p) || *p == ',') p++;
        if (*p == '}' || *p == '\0') break;

        if (*p != '\"') break;
        p++;
        char key[128];
        int ki = 0;
        while (*p && *p != '\"' && ki < (int)(sizeof(key)-1))
            key[ki++] = *p++;
        key[ki] = '\0';
        if (*p == '\"') p++;
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
        while (isspace((unsigned char)*p)) p++;

        char val[256];
        int vi = 0;
        if (*p == '\"') {
            // string value
            p++;
            while (*p && *p != '\"' && vi < (int)(sizeof(val)-1))
                val[vi++] = *p++;
            if (*p == '\"') p++;
        } else {
            // non‐string
            while (*p && *p != '}' && *p != ',' &&
                   !isspace((unsigned char)*p) &&
                   vi < (int)(sizeof(val)-1))
                val[vi++] = *p++;

        }
        val[vi] = '\0';

        J->properties->push_back(J->properties, strdup(key));
        J->values->push_back(J->values,     strdup(val));
    }
}

char* _jtoString(JSON* J) {
    size_t buf_size = 1024;
    char *ret = malloc(buf_size);
    if (!ret) return NULL;

    strcpy(ret, "{");

    StrDeque* properties = J->properties;
    StrDeque* values = J->values;

    StrNode* curProp = properties->head;
    StrNode* curVal = values->head;
    while (curProp) {
        // Append "key":value
        strcat(ret, "\"");
        strcat(ret, curProp->data);
        strcat(ret, "\":");
        strcat(ret, curVal->data);

        if (curProp->next) {
            strcat(ret, ",");
        }

        curProp = curProp->next;
        curVal = curVal->next;
    }

    strcat(ret, "}");
    return ret;
}

void _jset(JSON* J, char* property, char* value) {
    int idx = J->properties->find(J->properties, property);
    if (idx == -1) {
        J->properties->push_back(J->properties,property);
        J->values->push_back(J->values,value);
    } else {
        J->values->change(J->values, idx, value);
    }
}

char* _jget(JSON* J, char* property) {
    int idx = J->properties->find(J->properties, property);
    if (idx == -1) {
        MessageBoxA(NULL, "No such element in JSON", "ERROR", MB_ICONERROR);
        exit(0);
    } else {
        return J->values->at(J->values, idx);
    }
}

JSON* newJSON() {
    JSON* J = (JSON*)malloc(sizeof(JSON));
    J->properties = newStrDeque();
    J->values = newStrDeque();
    J->set = _jset;
    J->get = _jget;
    J->toString = _jtoString;
    J->load = _jload;
    return J;
}


JSON* POST(SOCKET S, JSON* J) {
    const char *msg = J->toString(J);   // '\n' 구분자 필수
    if (psend(S, msg, (int)strlen(msg), 0) == SOCKET_ERROR) {
        MessageBoxA(NULL, "send() 실패", "ERROR", MB_ICONERROR);
        pclosesocket(S);
        exit(0);
    }

    char buf[1024] = {0};
    int total = 0;
    while (total < sizeof buf - 1) {           // '\n' 올 때까지 모음
        int n = precv(S, buf + total, 1, 0);   // 1바이트씩 읽기(단순)
        if (n <= 0) break;
        if (buf[total++] == '\n') break;
    }
    buf[total-1] = '\0';
    gotoxy(0, 20); printf("[JSON LOGGER] %s \n", buf);

    JSON* response = newJSON();
    response->load(response, buf);
    return response;
}

// 파일에서 전체 JSON 로드 후 J에 채워넣고 반환
static JSON* loadStorage(void) {
    JSON* storage = newJSON();
    FILE* fp = fopen(LOCAL_STORAGE_FILE, "r");
    if (fp) {
        char buf[4096];
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        storage->load(storage, buf);
    }
    return storage;
}

// JSON을 직렬화해서 파일에 쓰기
static void saveStorage(JSON* storage) {
    char* out = storage->toString(storage);
    FILE* fp = fopen(LOCAL_STORAGE_FILE, "w");
    if (fp) {
        fprintf(fp, "%s", out);
        fclose(fp);
    }
    free(out);
}

void saveToLocalStorage(char* property, char* value) {
    JSON* storage = loadStorage();
    storage->set(storage, property, value);
    saveStorage(storage);
}

// 로컬스토리지에서 프로퍼티 읽기 (없으면 NULL 반환)
char* loadFromLocalStorage(char* property) {
    JSON* storage = loadStorage();
    // 존재하지 않으면 NULL, 있으면 strdup된 문자열 반환
    int idx = storage->properties->find(storage->properties, property);
    if (idx == -1) {
        return NULL;
    }
    return strdup(storage->values->at(storage->values, idx));
}

char *clientID;

int getCountInStr(char* source, char F) {
    int len = strlen(source);
    int ret = 0;
    int i;
    for (i = 0; source[i] != ';'; i++)
        ret += (source[i] == F);
    source[i] = '\0';
    return ret;
}


void multiPlayerGame() {
    clearScreen();
    /* 1) 연결 -------------------------------------------------------------- */
    SOCKET s = psocket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in srv = {0};
    srv.sin_family      = AF_INET;
    srv.sin_port        = phtons(12345);
    srv.sin_addr.s_addr = phtonl(0x67F4766E);

    gotoxy(0, 1);
    clientID = loadFromLocalStorage("clientID");
    if (clientID == NULL) {
        printf("저장된 클라이언트 ID 없음.\n");
    } else {
        printf("저장된 ID: %s\n", clientID);
    }

    system("pause > nul");
    clearScreen();

    gotoxy(0, 1);
    printf("서버에 연결하는 중...");

    if (pconnect(s, (struct sockaddr*)&srv, sizeof srv) == SOCKET_ERROR) {
        char buf[128];
        sprintf(buf, "connect 실패 : %d", pWSAGetLastError());
        MessageBoxA(NULL, buf, "ERROR", MB_ICONERROR);
        pclosesocket(s);
        exit(0);
    }

    /* 2) 전송 -------------------------------------------------------------- */
    {
        JSON* json = newJSON();
        json->set(json, "to", "'/check-connection'");
        json->set(json, "verify", "'SYN'");
        JSON* response = POST(s, json);
        gotoxy(0, 1); printf("[서버 응답 코드] %s \n", response->get(response, "status"));
        printf("[JSON] %s\n", response->get(response, "text"));

    }
    system("pause > nul");
    clearScreen();
    if (clientID == NULL)
    {
        JSON* json = newJSON();
        json->set(json, "to", "'/getClientID'");
        JSON* response = POST(s, json);
        if (strcmp(response->get(response, "status"),"200") == 0) {
            gotoxy(0, 1); printf("[발급된 ClientID] %s \n", response->get(response, "text"));
            saveToLocalStorage("clientID", response->get(response, "text"));
        } else {
            gotoxy(0, 1); printf("[ClientID 발급 중 에러] %s \n", response->get(response, "text"));
        }
    } else {
        gotoxy(0, 1); printf("[발급된 ClientID] %s \n", clientID);
    }
    system("pause > nul");
SELECT_ROOM:
    clearScreen();

    gotoxy(0, 1);
    printf("현재 방 목록");
    {
        JSON* json = newJSON();
        json->set(json, "to", "'/getRoomIDList'");
        JSON* roomIDListResponse = POST(s, json);
        char* roomIDList = roomIDListResponse->get(roomIDListResponse, "text");
        //printf("%s\n", roomIDList);

        int roomCount = getCountInStr(roomIDList, ',')+1;

        int* roomIDINTList = (int*)malloc(sizeof(int) * roomCount);
        JSON** roomInfoList = (JSON**)malloc(sizeof(JSON*) * roomCount);

        int i = 0;
        char *ptr = strtok(roomIDList,",");
        while (ptr != NULL) {
            roomIDINTList[i++] = atoi(ptr);
            ptr = strtok(NULL,",");
        }
        for (i=0;i<roomCount;i++) {
            JSON* rjson = newJSON();
            rjson->set(rjson, "to", "'/getRoomInfo'");
            char str[5];
            sprintf(str, "%d", roomIDINTList[i]);
            rjson->set(rjson, "roomID", str);

            JSON* response = POST(s, rjson);
            roomInfoList[i] = response;
            if (strcmp(response->get(response, "status"),"200") == 0) {
                gotoxy(0, 2+i*3); printf("┌────────────────────────────────────────────────────────────┐");
                gotoxy(0, 3+i*3); printf("│                                                            │");
                gotoxy(0, 4+i*3); printf("└────────────────────────────────────────────────────────────┘");
                gotoxy(3, 3+i*3); printf("%s", response->get(response, "name"));
                gotoxy(55, 3+i*3); printf("%s/2", response->get(response, "p_count"));
            } else {
                gotoxy(0, 1); printf("[에러] %s \n", response->get(response, "text"));
            }

        }
        gotoxy(0, 2+roomCount*3); printf("┌────────────────────────────────────────────────────────────┐");
        gotoxy(0, 3+roomCount*3); printf("│                                                            │");
        gotoxy(0, 4+roomCount*3); printf("└────────────────────────────────────────────────────────────┘");
        gotoxy(3, 3+roomCount*3); printf("+ 방 만들기");

        int selected = 0;
        setColor(LightGreen);
        gotoxy(0, 2+selected*3); printf("┌────────────────────────────────────────────────────────────┐");
        gotoxy(0, 3+selected*3); printf("│                                                            │");
        gotoxy(0, 4+selected*3); printf("└────────────────────────────────────────────────────────────┘");
        gotoxy(3, 3+selected*3); printf("%s", roomInfoList[selected]->get(roomInfoList[selected], "name"));
        gotoxy(55, 3+selected*3); printf("%s/2", roomInfoList[selected]->get(roomInfoList[selected], "p_count"));
        setColor(White);

        while (!isPressed(VK_RETURN))
        {
            int flag = 0;
            if (isPressed(VK_UP)) flag = -1;
            if (isPressed(VK_DOWN)) flag = 1;


            if (flag != 0) {
                setColor(White);
                gotoxy(0, 2+selected*3); printf("┌────────────────────────────────────────────────────────────┐");
                gotoxy(0, 3+selected*3); printf("│                                                            │");
                gotoxy(0, 4+selected*3); printf("└────────────────────────────────────────────────────────────┘");
                if (selected == roomCount) {
                    gotoxy(3, 3+selected*3); printf("+ 방 만들기");
                } else {
                    gotoxy(3, 3+selected*3); printf("%s", roomInfoList[selected]->get(roomInfoList[selected], "name"));
                    gotoxy(55, 3+selected*3); printf("%s/2", roomInfoList[selected]->get(roomInfoList[selected], "p_count"));
                }

                selected += flag;

                if (selected < 0) selected = roomCount;
                if (selected > roomCount) selected = 0;
                setColor(LightGreen);
                gotoxy(0, 2+selected*3); printf("┌────────────────────────────────────────────────────────────┐");
                gotoxy(0, 3+selected*3); printf("│                                                            │");
                gotoxy(0, 4+selected*3); printf("└────────────────────────────────────────────────────────────┘");
                if (selected == roomCount) {
                    gotoxy(3, 3+selected*3); printf("+ 방 만들기");
                } else {
                    gotoxy(3, 3+selected*3); printf("%s", roomInfoList[selected]->get(roomInfoList[selected], "name"));
                    gotoxy(55, 3+selected*3); printf("%s/2", roomInfoList[selected]->get(roomInfoList[selected], "p_count"));
                }
                setColor(White);

                Sleep(200);
            }
        }
        if (selected == roomCount) {
            // 방을 새로 만드는 경우
        } else {
            JSON* J = newJSON();
            J->set(J, "to", "'/joinRoom'");
            char clientIDM[20];
            sprintf(clientIDM, "'%s'", clientID);
            J->set(J, "clientID", clientIDM);
            char str[5];
            sprintf(str, "%d", roomIDINTList[selected]);
            J->set(J, "roomID", str);
            JSON* response = POST(s, J);
            if (strcmp(response->get(response, "status"), "200") != 0) goto SELECT_ROOM;

        }
    }




    /* 5) 종료 -------------------------------------------------------------- */
    pshutdown(s, SD_SEND);                     // FIN
    pclosesocket(s);


}

void lobby()
{

    COORD T= {0, 0};
    FillConsoleOutputCharacter(stdHandle, ' ', 300 * 300, T, &dw);
    Sleep(50);

    srand(time(0));

    setColor(Green);
    gotoxy(50, 3);puts("   _____             __           ______                   ");
    gotoxy(50, 4);puts("  / ___/____  ____ _/ /_____     / ____/___ _____ ___  ___ ");
    gotoxy(50, 5);puts("  \\__ \\/ __ \\/ __ `/ //_/ _ \\   / / __/ __ `/ __ `__ \\/ _ \\");
    gotoxy(50, 6);puts(" ___/ / / / / /_/ / ,< /  __/  / /_/ / /_/ / / / / / /  __/");
    gotoxy(50, 7);puts("/____/_/ /_/\\__,_/_/|_|\\___/   \\____/\\__,_/_/ /_/ /_/\\___/ ");

	setColor(White);
    gotoxy(70, 20);
    printf("> 싱글 플레이 (점수)");
    gotoxy(70, 22);
    printf("  멀티 플레이 (대결)");
    //gotoxy(70, 24);
    //printf("  리듬 게임");

    bool isSinglePlayer = true;
    while (!isPressed(VK_RETURN))
    {
        if (isPressed(VK_UP) || isPressed(VK_DOWN))
        {
            isSinglePlayer = !isSinglePlayer;
            if (isSinglePlayer)
            {
                gotoxy(70, 20);
                printf("> 싱글 플레이 (점수)");
                gotoxy(70, 22);
                printf("  멀티 플레이 (대결)");
            }
            else
            {
                gotoxy(70, 20);
                printf("  싱글 플레이 (점수)");
                gotoxy(70, 22);
                printf("> 멀티 플레이 (대결)");
            }
            Sleep(300);
        }
    }
    FillConsoleOutputCharacter(stdHandle, ' ', 300 * 300, T, &dw);
    if (isSinglePlayer)
    {
        player = newSnake();
        renderBorder();
        renderFirstSnake();
        player->addSpeed(player, 0);

        srand(time(0));

        HANDLE hRot = CreateThread(NULL,0,rotateSnakeThread,NULL,0,NULL);
        HANDLE hMov = CreateThread(NULL,0,moveSnakeThread,  NULL,0,NULL);

        WaitForSingleObject(hRot, INFINITE);
        WaitForSingleObject(hMov, INFINITE);
        CloseHandle(hRot);
        CloseHandle(hMov);

        GameOver();

    } else
    {
        multiPlayerGame();
        system("pause > nul");
        Sleep(500);
        lobby();
    }

}

int main(void)
{
    winsock_dynload();
    CONSOLE_CURSOR_INFO cursorInfo = { 0, };
    cursorInfo.dwSize = 1;
    cursorInfo.bVisible = 0;
    SetConsoleCursorInfo(stdHandle, &cursorInfo);
    HWND hwnd = GetConsoleWindow();
    Sleep(10);
    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner == NULL) {
        // Windows 10
        SetWindowPos(hwnd, NULL, 0, 0, 1500, 900, SWP_NOZORDER|SWP_NOMOVE);
    }
    else {
        // Windows 11
        SetWindowPos(owner, NULL, 0, 0, 1500, 900, SWP_NOZORDER|SWP_NOMOVE);
    }
    lobby();
}
