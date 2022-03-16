#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <strings.h>
#include <string.h>
#include <stdbool.h>
#include <sys/select.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <signal.h>
#include <math.h>
#include <ctype.h> 

GtkWidget *window;
GtkWidget *fixed1;

GtkWidget *buttonStart;
GtkWidget *buttonPlay;
GtkWidget *buttonLeave;
GtkWidget *buttonRules;
GtkWidget *buttonRevert;
GtkWidget *buttonSubmit;

GtkWidget *labelUsername;
GtkWidget *labelInfo;
GtkWidget *labelQuestion;
GtkWidget *labelRules;
GtkWidget *labelTimer;
GtkWidget *labelRecorded;
GtkWidget *labelWinner;
GtkWidget *labelOver;
GtkWidget *labelError;

GtkWidget *entryUsername;

GtkWidget *a;
GtkWidget *b;
GtkWidget *c;
GtkWidget *d;
GtkWidget *hide;

GtkBuilder *builder;

#define size 200

/*socket descriptor*/
int sd;
extern int errno;
int port;

bool questionReplied = false;
int secondsRemaining = 20;

char answer[size];
char response[10],text[20];
int answerLength;

bool finished = false;

void checkAnswer(char answer[size]);

int main(int argc,char *argv[])
{
    if(argc != 3){
        printf("[player]Syntax:<server address> <port>.\n");
        return -1;
    }
    
    struct sockaddr_in server;
    char command[size];
    port = atoi(argv[2]);

    if((sd = socket(AF_INET,SOCK_STREAM,0)) == -1){
        perror("[player]]Error at creating socket.\n");
        return errno;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    /*connect to server*/
    if(connect(sd,(struct sockaddr*)&server,sizeof(struct sockaddr)) == -1){
        perror("[player]Error at connect().\n");
        return errno;
    }

    gtk_init(&argc,&argv);

    GdkColor color;
    gdk_color_parse ("white", &color);

    builder = gtk_builder_new_from_file("interface.glade");
    window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));

    g_signal_connect(window,"destroy",G_CALLBACK(gtk_main_quit),NULL);
    gtk_builder_connect_signals(builder,NULL);

    fixed1 = GTK_WIDGET(gtk_builder_get_object(builder,"fixed1"));

    buttonStart = GTK_WIDGET(gtk_builder_get_object(builder,"buttonStart"));
    gdk_color_parse ("blue", &color);
    gtk_widget_modify_fg(GTK_WIDGET(buttonStart),GTK_STATE_NORMAL,&color);

    buttonPlay = GTK_WIDGET(gtk_builder_get_object(builder,"buttonPlay"));

    buttonLeave = GTK_WIDGET(gtk_builder_get_object(builder,"buttonLeave"));
    gdk_color_parse ("red", &color);
    gtk_widget_modify_fg(GTK_WIDGET(buttonLeave),GTK_STATE_NORMAL,&color);
    
    buttonRules = GTK_WIDGET(gtk_builder_get_object(builder,"buttonRules"));
    gdk_color_parse ("purple", &color);
    gtk_widget_modify_fg(GTK_WIDGET(buttonRules),GTK_STATE_NORMAL,&color);
    
    buttonRevert = GTK_WIDGET(gtk_builder_get_object(builder,"buttonRevert"));
    
    buttonSubmit = GTK_WIDGET(gtk_builder_get_object(builder,"buttonSubmit"));
    gdk_color_parse ("blue", &color);
    gtk_widget_modify_fg(GTK_WIDGET(buttonSubmit),GTK_STATE_NORMAL,&color);

    labelUsername = GTK_WIDGET(gtk_builder_get_object(builder,"labelUsername"));
    labelInfo = GTK_WIDGET(gtk_builder_get_object(builder,"labelInfo"));
    labelRules = GTK_WIDGET(gtk_builder_get_object(builder,"labelRules"));
    labelTimer = GTK_WIDGET(gtk_builder_get_object(builder,"labelTimer"));
    labelRecorded = GTK_WIDGET(gtk_builder_get_object(builder,"labelRecorded"));
    labelWinner = GTK_WIDGET(gtk_builder_get_object(builder,"labelWinner"));
    labelOver = GTK_WIDGET(gtk_builder_get_object(builder,"labelOver"));
    labelError = GTK_WIDGET(gtk_builder_get_object(builder,"labelError"));

    entryUsername = GTK_WIDGET(gtk_builder_get_object(builder,"entryUsername"));

    gdk_color_parse ("white", &color);
    labelQuestion = GTK_WIDGET(gtk_builder_get_object(builder,"labelQuestion"));
    
    a = GTK_WIDGET(gtk_builder_get_object(builder,"a"));
    gtk_widget_modify_fg(GTK_WIDGET(a),GTK_STATE_NORMAL,&color);
    
    b = GTK_WIDGET(gtk_builder_get_object(builder,"b"));
    gtk_widget_modify_fg(GTK_WIDGET(b),GTK_STATE_NORMAL,&color);
    
    c = GTK_WIDGET(gtk_builder_get_object(builder,"c"));
    gtk_widget_modify_fg(GTK_WIDGET(c),GTK_STATE_NORMAL,&color);
    
    d = GTK_WIDGET(gtk_builder_get_object(builder,"d"));
    gtk_widget_modify_fg(GTK_WIDGET(d),GTK_STATE_NORMAL,&color);
    
    gtk_widget_show_all(window);
    gtk_main();
}

static gboolean checkServerReady(gpointer data)
{
    fd_set read_fds;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    FD_ZERO(&read_fds);
    FD_SET(sd,&read_fds);
    if(select(sd+100,&read_fds,NULL,NULL,&tv) == -1){
        perror("[player]Error at select() in function checkServerReady()\n");
        return errno;
    }
    if(FD_ISSET(sd,&read_fds)){
        if(read(sd,&answerLength,sizeof(int)) <= 0){
            perror("[player]Error at reading length of answer.\n");
        }
        if(read(sd,answer,answerLength) <= 0){
            perror("[player]Error at reading answer.\n");
        }
        answer[answerLength]='\0';

        checkAnswer(answer);
    }
    else{ 
        //printf("[player]Nothing from server!\n");
        return TRUE;
    }
    return FALSE;
}

static gboolean questionTimer(gpointer data)
{
    if(secondsRemaining <= 0) {
        if(!questionReplied){
            printf("[player]Time expired, no answer sent!\n");
        }

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(a),FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(b),FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(c),FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(d),FALSE);

        gtk_widget_hide(a);
        gtk_widget_hide(b);
        gtk_widget_hide(c);
        gtk_widget_hide(d);
        gtk_widget_hide(labelQuestion);
        gtk_widget_hide(labelTimer);
        gtk_widget_hide(labelRecorded);
        gtk_widget_hide(buttonSubmit);

        strcpy(response,"");

        g_timeout_add(100, checkServerReady, NULL);

        return FALSE;
    }

    secondsRemaining--;
    sprintf(text,"Time remaining: %d",secondsRemaining);
    gtk_label_set_text(GTK_LABEL(labelTimer), (gchar*)(text));
    return TRUE;
}

void checkAnswer(char answer[size])
{
    if(strncmp(answer,"info:",5) == 0){

        gtk_widget_hide(labelError);
        gtk_label_set_text(GTK_LABEL(labelInfo),(gchar*)(answer+5));
        gtk_widget_show(labelInfo);

        g_timeout_add(500, checkServerReady, NULL);

    }
    else if(strncmp(answer,"winner:",7) == 0)
    {
        gtk_widget_hide(buttonLeave);
        gtk_label_set_text(GTK_LABEL(labelWinner),(gchar*)(answer+7));
        gtk_widget_show(labelWinner);

        finished = true;
        
        gtk_widget_show(labelOver);

        g_timeout_add(5000, checkServerReady, NULL);
    }
    else if(strncmp(answer,"end:",4) == 0)
    {
        /*jocul s-a incheiat*/
        close(sd);
        gtk_widget_destroy(window);
        exit(EXIT_SUCCESS);
    }
    else if(strncmp(answer,"Q:",2) == 0){

        strcpy(answer, answer+2);
        gtk_widget_hide(labelInfo);
        gtk_widget_hide(labelError);
        //gtk_widget_show(buttonLeave);

        /*extragere intrebare si variante de raspuns*/
        gtk_label_set_text(GTK_LABEL(labelQuestion),( gchar*)(strtok(answer,"\n")));
        gtk_button_set_label(GTK_BUTTON(a),( gchar*)(strtok(NULL,"\n")));
        gtk_button_set_label(GTK_BUTTON(b),( gchar*)(strtok(NULL,"\n")));
        gtk_button_set_label(GTK_BUTTON(c),( gchar*)(strtok(NULL,"\n")));
        gtk_button_set_label(GTK_BUTTON(d),( gchar*)(strtok(NULL,"\n")));

        gtk_widget_show(labelQuestion);
        gtk_widget_show(a);
        gtk_widget_show(b);
        gtk_widget_show(c);
        gtk_widget_show(d);
        gtk_widget_show(buttonSubmit);

        questionReplied = false;
        secondsRemaining = 20;

        sprintf(text, "Time remaining: %d", secondsRemaining);
        gtk_label_set_text(GTK_LABEL(labelTimer), (gchar*)(text));
        gtk_widget_show(labelTimer);

        /*functie ce se autoapeleaza (o data la aproape o secunda)*/
        g_timeout_add(950, questionTimer, NULL);
    }
    else if(strncmp(answer,"error:",6)==0){
        gtk_label_set_text(GTK_LABEL(labelError), (gchar*)(answer+6));
        gtk_widget_show(labelError);
    }
}

void on_buttonStart_clicked(GtkButton *b)
{
    gtk_widget_hide(buttonRules);
    gtk_widget_hide(GTK_WIDGET(b));
    gtk_widget_show(labelUsername);
    gtk_label_set_text(GTK_LABEL(labelUsername),"Enter username:");
    gtk_widget_show(entryUsername);
    gtk_widget_show(buttonPlay);
}

void on_entryUsername_changed(GtkEntry *e)
{

}

void on_buttonPlay_clicked(GtkButton *b)
{
    char username[50];
    if(strcmp(gtk_entry_get_text(GTK_ENTRY(entryUsername)),"")){
        sprintf(username,"start:%s",gtk_entry_get_text(GTK_ENTRY(entryUsername)));
    }
    else{
        gtk_label_set_text(GTK_LABEL(labelError),( gchar*)("you have to enter an username before you click play!"));
        gtk_widget_show(labelError);
        return;
    }

    gtk_widget_hide(labelUsername);
    gtk_widget_hide(entryUsername);
    gtk_widget_hide(buttonPlay);

    int length = strlen(username);
    if(send(sd,&length,sizeof(int),0) == -1){
        perror("[player]Error at sending length of command.\n");
    }
    if(send(sd,username,length,0) == -1){
        perror("[player]Error at sending command.\n");
    }

    /*verifica primire raspuns server*/
    g_timeout_add(80, checkServerReady, NULL);

}

void on_buttonRules_clicked(GtkButton *b)
{
    gtk_widget_hide(buttonStart);
    gtk_widget_hide(GTK_WIDGET(b));
    char rules[300];
    sprintf(rules,"    Rules of the game:\n1) A game session consists of 3 participants.\n2) The quizz contains 8 questions.\n3) For each question, the time allotted to receive an answer\n    is 20 seconds.\n4) Questions are single answer.\n5) The score is calculated according to the response time.");
    gtk_label_set_text(GTK_LABEL(labelRules), (gchar*)rules);
    gtk_widget_show(labelRules);
    gtk_widget_show(buttonRevert);
}

void on_buttonRevert_clicked(GtkButton *b)
{
    gtk_widget_hide(GTK_WIDGET(b));
    gtk_widget_hide(labelRules);

    gtk_widget_show(buttonStart);
    gtk_widget_show(buttonRules);
}

void on_buttonLeave_clicked(GtkButton *b)
{
    finished = true;
    int length = 5;

    if(send(sd,&length,sizeof(int),0) == -1){
        perror("[player]Error at sending length of command.\n");
    }
    if(send(sd,"leave",length,0) == -1){
        perror("[player]Error at sending command.\n");
    }
    
    g_timeout_add(50,checkServerReady,NULL);
}

void on_buttonA_toggled(GtkCheckButton *b)
{
    gboolean check=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b));
    if(check == true){
        strcpy(response,"R:a");
    }
}

void on_buttonB_toggled(GtkCheckButton *b)
{
    gboolean check=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b));
    if(check == true){
        strcpy(response,"R:b");
    }
}

void on_buttonC_toggled(GtkCheckButton *b)
{
    gboolean check=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b));
    if(check == true){
        strcpy(response,"R:c");
    }
}

void on_buttonD_toggled(GtkCheckButton *b)
{
    gboolean check = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b));
    if(check == true){
        strcpy(response,"R:d");
    }
}

void on_button_submit_clicked()
{
    if(strcmp(response,"") == 0){
        gtk_label_set_text(GTK_LABEL(labelError), (gchar*)("you have to choose an answer before you click submit!"));
        gtk_widget_show(labelError);
        return;
    }
    if(questionReplied == false){

        gtk_widget_show(labelRecorded);
        gtk_widget_hide(labelError);
        
        int length = strlen(response);
        if(send(sd,&length,sizeof(int),0) == -1){
            perror("[player]Error at sending length of command.\n");
        }
        if(send(sd,response,length,0) == -1){
            perror("[player]Error at sending command.\n");
        }
        
        questionReplied = true;
    }
}

void on_window_close(GtkWindow* w)
{
    if(finished == false)
    {
        int length = 5;
        if(send(sd,&length,sizeof(int),0) == -1){
            perror("[player]Error at sending length of command.\n");
        }
        if(send(sd,"leave",length,0) == -1){
            perror("[player]Error at sending command.\n");
        }
    }
}