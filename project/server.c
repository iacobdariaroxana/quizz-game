#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sqlite3.h> 
#include <sys/select.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <limits.h>

#define port 2022
#define size 200

extern int errno;

typedef struct threadInfo{
    unsigned int idThread;
    int clientDescriptor;
    int session;
    int playerNumber;
    char username[50];
    int points;
    int startQN;
}threadInfo;

int counter;
int sessionNumber;
int quizzNumber=1;
bool flag[5001];
pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t lockQ=PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t lockS=PTHREAD_MUTEX_INITIALIZER; 

int server();
void handleThreadError(char*,unsigned int);
void initialization();
void *threadRoutine(void*);
void startQuiz(struct threadInfo);
char sendQuestion(struct threadInfo,int);
void handlerLeave(struct threadInfo );
int insertRow(int,int,int,char[]);
void *threadInsert(void* arg);
int updateRow(int,int,int);
void *threadUpdate(void* arg);
int deleteRow(int ,int );
int getScore(int,int);
char* getWinner(int,int);
int getPlayerN(int,int);
void setUsername(char[],int,struct threadInfo*);
void configurePlayer(struct threadInfo*);
int emptyDatabase();

int main(int argc,char *argv[])
{
    emptyDatabase();
    initialization();
    server();
}

int server()
{
    struct sockaddr_in server;
    struct sockaddr_in client;

    /*socket descriptor*/
    int serverD;

    /*creating a socket*/
    if((serverD=socket(AF_INET,SOCK_STREAM,0)) == -1){
        perror("[server]Error at creating socket().\n");
        return errno;
    }

    /*using option SO_REUSEADDR*/
    int optval = 1;
    if(setsockopt(serverD,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof(optval)) == -1){
        perror("[server]Error at setsockopt().\n");
        return errno;
    }

    bzero(&server,sizeof(server));
    bzero(&client,sizeof(client));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    /*assign a local address to the socket*/
    if(bind(serverD,(struct sockaddr*)&server,sizeof(struct sockaddr)) == -1){
        perror("[server]Error at bind().\n");
        return errno;
    }

    /*mark serverD as a passive socket*/
    if(listen(serverD,2) == -1){
        perror("[server]Error at listen().\n");
        return errno;
    }

    printf("Waiting for connections at port: %d.\n",port); fflush(stdout);

    unsigned int id = 0;

    while(true){
        int clientDescriptor;
        threadInfo* threadArg;
        int addrlen = sizeof(client);
        if((clientDescriptor = accept(serverD,(struct sockaddr*)&client,&addrlen)) == -1){
            perror("Error at accept().\n");
            continue;
        }

        if(sessionNumber == 5000){
            initialization();
        }
        
        threadArg = (struct threadInfo*)malloc(sizeof(struct threadInfo));
        if(id == UINT_MAX-1) id = 0;

        threadArg->idThread = id++;
        threadArg->clientDescriptor = clientDescriptor;
        pthread_t t;
        if(pthread_create(&t,NULL,&threadRoutine,threadArg)){
            perror("[server]Error at pthread_create().\n");
            return errno;
        }
    }
}

void initialization()
{
    counter=0;
    sessionNumber=1;
    for(int i=0;i<5001;i++){
        flag[i]=false;
    }
}

void handleThreadError(char errorMessage[100], unsigned int threadID)
{
    char copy[100];
    sprintf(copy,"[thread %u] %s\n",threadID,errorMessage);
    perror(copy);
    printf("[thread %u]Finished!\n",threadID);
    pthread_exit(NULL);
}

void *threadRoutine(void* arg)
{
    struct threadInfo info;
    info = *((struct threadInfo*)arg);

    printf("[thread %d]Waiting command.\n",info.idThread);
    fflush(stdout);

    if(pthread_detach(pthread_self())){
        handleThreadError("Error at detach().",info.idThread);
    }	

    int length;
    char command[size];
    while(true)
    {
        bzero(command,size);
        if(recv(info.clientDescriptor,&length,sizeof(int),0) <= 0){
            handleThreadError("Error at reading length of command.",info.idThread);
        } 
        if(recv(info.clientDescriptor,command,length,0) <= 0){
            handleThreadError("Error at reading command.",info.idThread);
        }

        /* check type of command*/
        if(strncmp(command,"start:",6) == 0 && command[6] != '\0'){

            /*get username of player*/
            setUsername(command,length,&info);
            /*assign a session and a player number*/
            configurePlayer(&info);

            info.points = 0;
            /*create a separate thread who's job is to insert the player's info in database*/
            pthread_t threadID;
            if(pthread_create(&threadID,NULL,&threadInsert,&info)){
                handleThreadError("Error at pthread_create() for insert in database.",info.idThread);
            }

            length = 43;
            if(send(info.clientDescriptor,&length,sizeof(int),0) <= 0){
                handleThreadError("Error at writing length of response.",info.idThread);
            }
            
            if(send(info.clientDescriptor,"info:wait other players to join the game...",length,0) <= 0){
                handleThreadError("Error at writing response.",info.idThread);
            }

            fd_set read_fds;
            struct timeval tv;
            /*wait until enough players gather to start the round*/
            while(flag[info.session] == false){

                /*verifica daca este ceva de citit pe socket cat timp jucatorul asteapta ceilalti participanti pentru a incepe runda(s-ar putea primi "leave")*/
                tv.tv_sec = 0;
                tv.tv_usec = 400000;

                FD_ZERO(&read_fds);
                FD_SET(info.clientDescriptor, &read_fds);
                if(select(info.clientDescriptor+1,&read_fds,NULL,NULL,&tv) == -1){
                    handleThreadError("Error at select() while waiting game to start.",info.idThread);
                }
                if(FD_ISSET(info.clientDescriptor,&read_fds)){
                    if(read(info.clientDescriptor,&length,sizeof(int)) <= 0){
                        handleThreadError("Error at reading length of command.",info.idThread);
                    }
                    if(read(info.clientDescriptor,command,length) <= 0){
                        handleThreadError("Error at reading command",info.idThread);
                    }
                    command[length] = '\0';
                    if(strncmp(command,"leave",5) == 0){
                        handlerLeave(info);
                    }
                }
                // else{
                //     printf("[thread %d]Nothing from client!\n",info.idThread);
                // }
            }

            /*s-au adunat suficienti participanti pentru o runda->incepe jocul*/
            startQuiz(info);
        }
        else if(strncmp(command,"leave",5) == 0){
            handlerLeave(info);
        }
        else{
            length = 29;
            if(send(info.clientDescriptor,&length,sizeof(int),0) <= 0){
               handleThreadError("Error at writing length of response.",info.idThread);
            }
            if(send(info.clientDescriptor,"error:unrecognized command.\n",length,0) <= 0){
                handleThreadError("Error at writing response.",info.idThread);
            }
        }
    }
}

void configurePlayer(struct threadInfo* info)
{
    /*folosirea unui lacat pentru a asigura acuratetea informatiilor*/
    pthread_mutex_lock(&lock);
    info->session = sessionNumber;
    info->startQN = quizzNumber;
    counter++;
    info->playerNumber = counter;
    if(counter == 3){
        flag[sessionNumber] = true;
        sessionNumber++;
        quizzNumber += 8;
        if(quizzNumber >= 49 ) quizzNumber = 1;
        counter = 0;
    }
    pthread_mutex_unlock(&lock);
}

void *threadInsert(void* arg)
{
    struct threadInfo info;
    info = *((struct threadInfo*)arg);
    if(pthread_detach(pthread_self())){
        handleThreadError("Error at detach inside insert thread.",info.idThread);
    }	

    pthread_mutex_lock(&lockS);
    insertRow(info.session,info.playerNumber,info.points,info.username);
    pthread_mutex_unlock(&lockS);

    pthread_exit(NULL);
}

void *threadUpdate(void* arg)
{
    struct threadInfo info;
    info =* ((struct threadInfo*)arg);
    if(pthread_detach(pthread_self())){
        handleThreadError("Error at detach inside update thread.",info.idThread);
    }	
    pthread_mutex_lock(&lockS);
    updateRow(info.session, info.playerNumber, info.points);
    pthread_mutex_unlock(&lockS);
    pthread_exit(NULL);
}

void *threadDelete(void* arg)
{
    sleep(5); /*to make sure that the client's score is not deleted before being questioned by the other players*/

    struct threadInfo info;
    info =* ((struct threadInfo*)arg);
    if(pthread_detach(pthread_self())){
        handleThreadError("Error at detach inside delete thread.",info.idThread);
    }	

    pthread_mutex_lock(&lockS);
    deleteRow(info.session, info.playerNumber);
    pthread_mutex_unlock(&lockS);

    pthread_exit(NULL);
}

void startQuiz(struct threadInfo info)
{
    time_t start,end;
    double secondsElapsed;
    int length;
    char command[size], answer[size];
    char correctAnswer;

    /*using select() for giving player 20 seconds to answer*/
    fd_set readfds;
    int nfds;
    struct timeval tv;
    nfds = info.clientDescriptor;

    /*start sending questions*/
    for(int i = info.startQN; i < info.startQN+8; i++){

        /*send question and receive the correct answer for that question*/
        correctAnswer = sendQuestion(info,i);

        /*wait 20 seconds for response*/
        bzero(command,size);
        FD_ZERO(&readfds);
        FD_SET(info.clientDescriptor, &readfds);
        tv.tv_sec = 20;
        tv.tv_usec = 0;

        time(&start);
        if(select(nfds+1,&readfds,NULL,NULL,&tv) < 0){
            handleThreadError("Error at select().",info.idThread);
        }
        time(&end);
        secondsElapsed = (double)(end-start);

        /*verify if player sent response to question*/
        if(FD_ISSET(info.clientDescriptor,&readfds)){

            if(recv(info.clientDescriptor,&length,sizeof(int),0) <= 0){
                handleThreadError("Error at reading length of command.",info.idThread);
            }
            if(recv(info.clientDescriptor,command,size,0) <= 0){
                handleThreadError("Error at reading command.",info.idThread);
            }  

            printf("[thread %u]Answer received after %f seconds(%s)-correct answer %c.\n",info.idThread,secondsElapsed,command,correctAnswer);fflush(stdout);

            if(strncmp(command,"R:",2) == 0){
                if(command[2] == correctAnswer){
                    info.points += 20-(int)secondsElapsed;
                    printf("[thread %d]Points: %d\n",info.idThread,info.points); fflush(stdout);

                    /*create a separate thread to update the value in database*/
                    pthread_t threadID;
                    if(pthread_create(&threadID,NULL,&threadUpdate,&info)){
                        handleThreadError("Error at pthread_create() for update.",info.idThread);
                    }

                }
            }
            else if(strncmp(command,"leave",5) == 0){
                handlerLeave(info);
            }
        }
        else{
            printf("[thread %u]Time expired, no answer received.\n",info.idThread);
            fflush(stdout);
        }

        /*in cazul in care a mai ramas timp dupa submit jucatorul are optiunea de a parasi jocul (verificare primire cerere)*/
        if(secondsElapsed != 20){
            tv.tv_sec -= (int)(end-start);
            tv.tv_usec = 0;

            bzero(command,size);
            FD_ZERO(&readfds);
            FD_SET(info.clientDescriptor,&readfds);
            if(select(nfds+1,&readfds,NULL,NULL,&tv) < 0){
                handleThreadError("Error at select().",info.idThread);
            }
            if(FD_ISSET(info.clientDescriptor,&readfds)){
                if(recv(info.clientDescriptor,&length,sizeof(int),0) <= 0){
                    handleThreadError("Error at reading length of command.",info.idThread);
                }
                if(recv(info.clientDescriptor,command,size,0) <= 0){
                    handleThreadError("Error at reading command.",info.idThread);
                }  
                if(strncmp(command,"leave",5) == 0){
                    handlerLeave(info);
                }
            }
        }
    }

    sleep(2);
    int myScore, score, maximumScore=0, winnerNumber,PN;

    for(int i = 1; i <= 3; i++){
        score=getScore(info.session,i);
        if(score > maximumScore){
            maximumScore = score;
            PN = i;
        }
        if(i == info.playerNumber) myScore = score;
    }

    if(maximumScore == 0){
        sprintf(answer,"winner:Draw\nscore:%d",myScore);
    }
    else if(myScore == maximumScore){
        sprintf(answer,"winner:Congratulations!You won!\n      score:%d \n",myScore);
    }
    else
    {
        //winnerNumber=getPlayerN(info.session,maximumScore);
        char username[50];
        strcpy(username, getWinner(info.session,PN));
        sprintf(answer, "winner:Player %s won!\nYour score:%d\nWinner:%d", username, myScore, maximumScore);
    }

    /*anuntare castigator*/
    length = strlen(answer);
    if(send(info.clientDescriptor,&length,sizeof(int),0) <= 0){
        handleThreadError("Error at writing length of response.",info.idThread);
    }
    if(send(info.clientDescriptor,answer,length,0) <= 0){
        handleThreadError("Error at writing response.",info.idThread);
    }

    /*anunta sfarsitul jocului*/
    length = 16;
    if(send(info.clientDescriptor,&length,sizeof(int),0) <= 0){
        handleThreadError("Error at writing length of response.",info.idThread);
    }
    if(send(info.clientDescriptor,"end:game is over",length,0) <= 0){
        handleThreadError("Error at writing response.",info.idThread);
    }

    /*crearea unui thread pentru a elimina din baza de date inregistrarea corespunzatoare playerului*/
    pthread_t threadID;
    if(pthread_create(&threadID,NULL,&threadDelete,&info)){
        handleThreadError("Error at pthread_create() for delete.",info.idThread);
    }

    /*done with this client, close connection*/
    close(info.clientDescriptor);
    printf("[thread %u] finished.\n", info.idThread);
    pthread_exit(NULL);
}

void setUsername(char command[],int length,struct threadInfo* info)
{
    /*preluare username din comanda de start*/
    int i = 6,j = 0;
    while(j<(length-6)){
        info->username[j++] = command[i++];
    }
    info->username[j] = '\0';

    printf("[thread %u]received username:%s\n",info->idThread,info->username);
    fflush(stdout);
}

char sendQuestion(struct threadInfo info,int id)
{ 
    sqlite3 *database;
    sqlite3_stmt *preparedStatement;
    
    int rValue = sqlite3_open("questions.db", &database);
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread %d ]Error at opening database: %s\n",info.idThread,sqlite3_errmsg(database));
        sqlite3_close(database);
    }

    char statement[50];
    sprintf(statement,"SELECT * from QUIZ WHERE ID=%d",id);
    rValue = sqlite3_prepare_v2(database, statement, -1, &preparedStatement, 0);    
    
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread %d]Error at preparing statement: %s\n",info.idThread,sqlite3_errmsg(database));
        sqlite3_close(database);
    }    

    rValue = sqlite3_step(preparedStatement);

    char question[200];
    question[0] = 'Q';
    question[1] = ':';
    question[2] = ' ';
    question[3] = '\0';

    if (rValue == SQLITE_ROW) {
        int numCol = sqlite3_column_count(preparedStatement);
        int i = 1;
        while(i < numCol)
        {
            //printf("%s\n", sqlite3_column_text(preparedStatement, i));
            strcat(question, sqlite3_column_text(preparedStatement,i));
            strcat(question, "\n");
            i++;
        }
    }
    sqlite3_finalize(preparedStatement);
    sqlite3_close(database);

    int length = strlen(question)-3;
    if(send(info.clientDescriptor,&length,sizeof(int),0) <= 0){
        handleThreadError("Error at sending length question to player.",info.idThread);
    }
    if(send(info.clientDescriptor,question,length,0) <= 0){
        handleThreadError("Error at sending question to player.",info.idThread);
    }

    /*return the letter coresponding to the right answer*/
    return question[strlen(question)-2];
}

void handlerLeave(struct threadInfo info)
{
    /*trimite confirmare cerere de parasire a jocului*/
    int length = 39;
    if(send(info.clientDescriptor,&length,sizeof(int),0) <= 0){
        handleThreadError("Error at writing length of response.",info.idThread);
    }
    if(send(info.clientDescriptor,"end:request to leave the game received\n",length,0) <= 0){
        handleThreadError("Error at writing response.",info.idThread);
    }

    /*done with this client, close connection*/
    close(info.clientDescriptor);
    
    pthread_mutex_lock(&lockS);
    deleteRow(info.session,info.playerNumber);
    pthread_mutex_unlock(&lockS);

    printf("[thread %u] finished.\n",info.idThread);
    pthread_exit(NULL);
}

int insertRow(int session,int PN,int points,char username[])
{
    sqlite3 *database;
    sqlite3_stmt *preparedStatement;
    char statement[150];

    int rValue = sqlite3_open("scores.db", &database);
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread insert]Error at opening database: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }
    
    sprintf(statement,"INSERT INTO SCORE VALUES(%d,%d,%d,'%s');", session, PN, points, username);
    rValue = sqlite3_prepare_v2(database, statement, -1, &preparedStatement, 0);    
    
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread insert]Error at preparing statement: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }    
    
    rValue = sqlite3_step(preparedStatement);

    if(rValue != SQLITE_DONE){
        fprintf(stderr, "[thread insert]Error at inserting row in database: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }

    sqlite3_reset(preparedStatement);
    sqlite3_finalize(preparedStatement);
    sqlite3_close(database);
    return 1;
}

int updateRow(int session,int PN,int points)
{
    sqlite3 *database;
    sqlite3_stmt *preparedStatement;
    char statement[150];

    int rValue = sqlite3_open("scores.db", &database);
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread]Error at opening database: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }

    sprintf(statement,"UPDATE SCORE SET POINTS=%d WHERE SESSION=%d AND PN=%d", points, session, PN);
    rValue = sqlite3_prepare_v2(database, statement, -1, &preparedStatement, 0);    
    
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread]Error at preparing statement: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }    
    
    rValue = sqlite3_step(preparedStatement);

    if(rValue != SQLITE_DONE){
        fprintf(stderr, "[thread]Error at updating row in database: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }

    sqlite3_reset(preparedStatement);
    sqlite3_finalize(preparedStatement);
    sqlite3_close(database);
    return 1;
}

int deleteRow(int session,int PN)
{
    sqlite3 *database;
    sqlite3_stmt *preparedStatement;
    char statement[150];

    int rValue = sqlite3_open("scores.db", &database);
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread delete]Error at opening database: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }

    sprintf(statement,"DELETE FROM SCORE WHERE SESSION=%d AND PN=%d", session, PN);
    rValue = sqlite3_prepare_v2(database, statement, -1, &preparedStatement, 0);    
    
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread delete]Error at preparing statement: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }    
    
    rValue = sqlite3_step(preparedStatement);

    if(rValue != SQLITE_DONE){
        fprintf(stderr, "[thread]Error at deleting row from database: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }

    sqlite3_reset(preparedStatement);
    sqlite3_finalize(preparedStatement);
    sqlite3_close(database);
    return 1;
}

int emptyDatabase()
{
    sqlite3 *database;
    sqlite3_stmt *preparedStatement;
    char statement[150];

    int rValue = sqlite3_open("scores.db", &database);
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[main server]Error at opening database: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }

    sprintf(statement,"DELETE FROM SCORE");
    rValue = sqlite3_prepare_v2(database, statement, -1, &preparedStatement, 0);    
    
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[main server]Error at preparing statement: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }    
    
    rValue = sqlite3_step(preparedStatement);

    if(rValue != SQLITE_DONE){
        fprintf(stderr, "[main server]Error at all rows from database: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }

    sqlite3_reset(preparedStatement);
    sqlite3_finalize(preparedStatement);
    sqlite3_close(database);
    return 1;
}

int getScore(int session,int PN)
{
    sqlite3 *database;
    sqlite3_stmt *preparedStatement;
    char statement[150];
    int points = 0;

    int rValue = sqlite3_open("scores.db", &database);
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread]Error at opening database in getScore(): %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
    }
    
    sprintf(statement,"SELECT POINTS FROM SCORE WHERE SESSION=%d AND PN=%d",session,PN);
    rValue = sqlite3_prepare_v2(database, statement, -1, &preparedStatement, 0);    
    
    if (rValue!= SQLITE_OK) {
        fprintf(stderr, "[thread]Error at preparing statement in getScore(): %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
    }    
    
    rValue = sqlite3_step(preparedStatement);
    if (rValue== SQLITE_ROW) {
        //printf("%s\n", sqlite3_column_text(preparedStatement,0));
        points=atoi(sqlite3_column_text(preparedStatement,0));
    }
    else
    {
        /*in cazul in care un jucator a parasit jocul si nu se mai regaseste in tabela de scor, punctajul lui este -1(pentru clasament)*/
        if(rValue==SQLITE_DONE){
            points=-1; 
        }
    }
    sqlite3_reset(preparedStatement);
    sqlite3_finalize(preparedStatement);
    sqlite3_close(database);

    printf("(%d , %d) points: %d\n",session,PN,points);fflush(stdout);
    
    return points;
}

int getPlayerN(int session,int points)
{
    sqlite3 *database;
    sqlite3_stmt *preparedStatement;

    char statement[150];
    int PN;
    
    int rValue = sqlite3_open("scores.db", &database);
    if (rValue != SQLITE_OK){
        fprintf(stderr, "[thread]Error at opening database in getPlayerN(): %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
    }
    
    sprintf(statement,"SELECT PN FROM SCORE WHERE SESSION=%d AND POINTS=%d",session,points);
    rValue = sqlite3_prepare_v2(database, statement, -1, &preparedStatement, 0);    
    
    if (rValue!= SQLITE_OK) {
        fprintf(stderr, "[thread]Error at preparing statement in getPlayerN(): %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
    }    
    
    rValue = sqlite3_step(preparedStatement);
    if (rValue == SQLITE_ROW) {
        PN=atoi(sqlite3_column_text(preparedStatement,0));
        //printf("PN:%d\n",PN);fflush(stdout);
    }
    else
    {
        if(rValue == SQLITE_DONE){
            PN=-1;
        }
    }
    sqlite3_reset(preparedStatement);
    sqlite3_finalize(preparedStatement);
    sqlite3_close(database);
    
    return PN;
}

char* getWinner(int session,int PN)
{
    sqlite3 *database;
    sqlite3_stmt *preparedStatement;
    char statement[size];
    char username[100];
    char *p;

    int rValue = sqlite3_open("scores.db", &database);
    if (rValue != SQLITE_OK) {
        fprintf(stderr, "[thread]Error at opening database in getWinner(): %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
    }
    sprintf(statement,"SELECT USERNAME FROM SCORE WHERE SESSION=%d AND PN=%d",session,PN);
    rValue = sqlite3_prepare_v2(database, statement, -1, &preparedStatement, 0);    
    
    if (rValue!= SQLITE_OK) {
        fprintf(stderr, "[thread]Error at preparing statement in getWinner(): %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
    }    
    
    rValue = sqlite3_step(preparedStatement);
    int length;
    if (rValue == SQLITE_ROW) {
        strcpy(username,sqlite3_column_text(preparedStatement,0));
    }
    sqlite3_reset(preparedStatement);
    sqlite3_finalize(preparedStatement);
    sqlite3_close(database);
    p = username;
    return p;
}