// Интерпретатор команд
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <fcntl.h>

// команды блока
struct BlockCmd {
    struct BlockCmd *Next; // указатель на следующую команду в блоке
    char **Words; // указатель на массив слов в команде
    int iNumWords; // длина команды, т.е. число слов в команде
    char *InputCmd; // вход команды
    char *OutputCmd; // выход команды
    int isAdd; // 0 - нет флага O_APPEND, 1 - есть флаг O_APPEND
};

// список блоков команд
struct ListCmd {
    struct ListCmd *Next; // указатель на следующий блок команд
    struct BlockCmd *BCmd; // указатель на текущий блок команд
    int isBack; // 1 - фоновый режим, 0 - обычный
};

// ----- Печать приветствия
void PrintWelcome() {
    char Path[2048];
    getcwd(Path, 2048); // считываем имя текущей директории
    printf("%s $", Path);
}

// ----- Печать команд
// указатель на список блоков команд
void PrintCommands(struct ListCmd *LCmd) {
    struct BlockCmd *BCmd; // указатель на команды блока
    char **Words; // указатель на массив слов в команде
    int i = 0;

    while (LCmd != NULL) {
        BCmd = LCmd->BCmd;
        while (BCmd != NULL) {
            Words = BCmd->Words;
            i = 0;
            while (Words[i] != NULL) {
                printf("%s ", Words[i++]);
            }
            if (BCmd->InputCmd) printf("< %s ",BCmd->InputCmd);
            if (BCmd->OutputCmd) {
                if (BCmd->isAdd) printf(">> %s ",BCmd->OutputCmd);
                else printf("> %s ",BCmd->OutputCmd);
            }
            BCmd = BCmd->Next;
            if (BCmd) printf("| ");
        }
        printf("\n");
        LCmd = LCmd->Next;
    }
}

// ----- Добавление символа в слово
// Word - указатель на слово
// iC - символ, который нужно добавить
char *AddCharToWord(char *Word, int iC) {
    size_t iLength = strlen(Word); // длина текущего слова

    Word = (char *) realloc(Word, (iLength + 2) * sizeof(char)); // +2 с учетом конца строки и следующего символа
    Word[iLength] = (char)iC; // записываем текущий символ в массив
    Word[iLength+1] = 0; // последний символ строки - 0
    return Word;
}

// ----- Считывание текущего слова
// iEnd - признак конца команды, т.е. дошли ли до \n или EOF, 1 - если дошли, 0 - в противном случае
char *GetWord(int *iEnd) {
    char *Word;
    int iC; // текущий символ
    int iQuoteOpen = 0; // открыта кавычка - 1, нет - 0
    int iQuoteOpenDouble = 0; // открыта двойная кавычка - 1, нет - 0

    Word = (char *) malloc(20 * sizeof(char));
    Word[0] = 0; // конец строки - 0

    // пока не дошли до пробела, до конца строки или до конца файла
    // если кавычка открыта, то разделители игнорируем

    while ((((iC = getchar()) != '\n') && (iC != ' ') && (iC != EOF)) || (iQuoteOpen == 1) || (iQuoteOpenDouble == 1)) {
        // если встретили \ ,то проверяем следующий символ; если enter, то считываем символ, иначе проверка на "
        if (iC == '\\') {
            iC = getchar();
            if (iC != '\n') Word = AddCharToWord(Word, iC);
         }
        else {
            // если встречена ', и открыта ", то записываем в команду, иначе инвертируем флаг
            if (iC == '\'') {
                if (iQuoteOpenDouble) Word = AddCharToWord(Word, iC);
                else iQuoteOpen = !iQuoteOpen;
            }
            // проверка на ", если встречена ", и открыта ', то " - спец.символ, записываем, иначе инвертируем флаг
            else if (iC == '\"') {
                if (iQuoteOpen) Word = AddCharToWord(Word, iC);
                else iQuoteOpenDouble = !iQuoteOpenDouble;
            } else Word = AddCharToWord(Word, iC); // считали обычный символ и его записываем
        }
    }
    *iEnd = (iC == '\n') || (iC == EOF) ;
    return Word;
}

// ----- Сравнивает слово со спецсимволами в виде строки и возвращает либо этот символ, либо 0 в противном случае
// Word - указатель на слово
char WhatIsWord(char *Word) {
    if (strcmp(Word,"&") == 0) return('&');
    if (strcmp(Word,"|") == 0) return('|');
    if (strcmp(Word,"\n") == 0) return('\n');
    if (strcmp(Word,"<") == 0) return('<');
    if (strcmp(Word,">") == 0) return('>');
    if (strcmp(Word,">>") == 0) return('A');
    return('0');
}

// ----- Добавить новый блок команд при чтении &
// LCmd - указатель на список блоков команд
struct ListCmd *AddListCmd(struct ListCmd *LCmd) {
    struct ListCmd *LCmdTmp = (struct ListCmd *) malloc(sizeof(struct ListCmd));
    LCmd->Next = LCmdTmp; LCmd->isBack = 1;
    LCmdTmp->Next = NULL; LCmdTmp->isBack = 0;

    struct BlockCmd *BCmd = (struct BlockCmd *) malloc(sizeof(struct BlockCmd));
    BCmd->Next = NULL;
    BCmd->Words = NULL;
    BCmd->iNumWords = 0;
    BCmd->InputCmd = NULL;
    BCmd->OutputCmd = NULL;
    BCmd->isAdd = 0;

    LCmdTmp->BCmd = BCmd;

    return LCmdTmp;
}

// ----- Добавить новый блок команд при чтении |
// BCmd - указатель на блочную команду
struct BlockCmd *AddBlockCmd(struct BlockCmd *BCmd) {
    struct BlockCmd *BCmdTmp = (struct BlockCmd *) malloc(sizeof(struct BlockCmd));
    BCmd->Next = BCmdTmp;
    BCmdTmp->Next = NULL;
    BCmdTmp->Words = NULL;
    BCmdTmp->iNumWords = 0;
    BCmdTmp->InputCmd = NULL;
    BCmdTmp->OutputCmd = NULL;
    BCmd->isAdd = 0;

    return BCmdTmp;
}

// ----- Добавить слово в блок команд
// BCmd - указатель на блочную команду
// Word - указатель на слово
char **AddWordToBlockCmd(struct BlockCmd *BCmd, char *Word) {
    // создаем блочную комнаду, если ее нет
    if (BCmd == NULL) {
        struct BlockCmd *BCmdTmp = (struct BlockCmd *) malloc(sizeof(struct BlockCmd));
        BCmd = BCmdTmp;
        BCmd->Next = NULL;
        BCmd->Words = NULL;
        BCmd->iNumWords = 0;
        BCmd->InputCmd = NULL;
        BCmd->OutputCmd = NULL;
        BCmd->isAdd = 0;
    }

    BCmd->iNumWords++; // увеличиваем число слов в команде
    BCmd->Words = (char **) realloc(BCmd->Words, (BCmd->iNumWords+1) * sizeof(char *));
    BCmd->Words[BCmd->iNumWords - 1] = Word;
    BCmd->Words[BCmd->iNumWords] = NULL;

    return BCmd->Words;
}

// ----- Возвращает число команд в блоке
// BCmd - указатель на блочную команду
int CmdCount (struct BlockCmd *BCmd) {
    int iCmdCount = 1;
    while (BCmd->Next != NULL) {
        iCmdCount++;
        BCmd = BCmd->Next;
    }
    return iCmdCount;
}

// ----- Выполнение команд блока
// BCmd - указатель на блочную команду
// isBack - 1 если в фоновом режиме, 0 - в обычном
// fIn - дескриптор для чтения, 0 - если первая команда и входного потока от другой команды нет
// PID - массив идентификаторов процессов
// iLevel - уровень вложенности рекурсивной функции (номер команды в блоке, которую выполняем), для первого вызова iLevel = 0
void ExecBCmdPipe(struct BlockCmd *BCmd, int isBack, int fIn, int *PID, int iLevel) {
    int iResult;
    int iProcess; // идентификатор процесса
    char **Words = BCmd->Words; // указатель на 1-ое слово блока
    char *Path;
    int i = 0;
    int iCount = CmdCount(BCmd);

    // выделяем память под массив идентификаторов процессов
    if (iLevel == 0) PID = malloc(sizeof(int)*(iCount-1));

    int fd[2]; // файловые дескрипторы для нового канала

    int fdIn = fIn; // файловый дескриптор для ввода
    int fdOut = -1; // файловые дескрипторы для вывода

    if (strcmp(Words[0], "cd") == 0) {
        Path = Words[1];
        iResult = chdir(Path);
        // если chdir возвращает 0, то успешно перешли к каталогу, иначе ошибка
        if (iResult == 0) {
            printf("Текущим стал каталог %s\n", Path);
        } else {
            printf("Не могу перейти к каталогу %s\n", Path);
        }
    }
    else {
        if (BCmd->Next != NULL) pipe(fd); // если есть следующая команда в списке, то создаем новый канал

        if ((iProcess = fork()) < 0) {
            printf("Ошибка!");
            exit(1);
        }
        else {
            if (iProcess == 0) {
                // выполнение внутри процесса потомка
                if (fdIn) {
                    dup2(fdIn, 0); // перенаправляем вход канала в стандартный поток ввода
                    close(fdIn);
                }
                if (BCmd->Next != NULL) {
                    dup2(fd[1], 1); // перенаправляем выход команды в канал(вход)
                    close(fd[1]);
                    close(fd[0]);
                }
                if (BCmd->InputCmd) {
                    fdIn = open(BCmd->InputCmd, O_RDONLY, 0777);
                    if (fdIn == -1) {
                        printf("Ошибка!");
                        exit(1);
                    }
                    else {
                        dup2(fdIn, 0);
                        close(fdIn);
                    }
                } // <
                if (BCmd->OutputCmd) {
                    if (BCmd->isAdd) {
                        fdOut = open(BCmd->OutputCmd, O_WRONLY | O_CREAT | O_APPEND, 0777);
                        if (fdOut == -1) {
                            printf("Ошибка!");
                            exit(1);
                        }
                        else {
                            dup2(fdOut, 1);
                            close(fdOut);
                        }
                    } // >>
                    else {
                        fdOut = open(BCmd->OutputCmd, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                        if (fdOut == -1) {
                            printf("Ошибка!");
                            exit(1);
                        }
                        else {
                            dup2(fdOut, 1);
                            close(fdOut);
                        }
                    } // >
                }

                execvp(Words[0], Words); // выполнение команды
                exit(0); // окончание процесса потомка
            }
            else {
                // выполнение внутри родительского процесса
                if (isBack) waitpid(-1, NULL, WNOHANG); // ждем окончания процессов потомков для фонового режима
                PID[iLevel] = iProcess;
                if (BCmd->Next != NULL) {
                    close(fd[1]); // закрываем дескриптор записи в канал
                    ExecBCmdPipe(BCmd->Next, isBack,fd[0], PID, iLevel+1); // запускаем функцию еще раз, для следующей команды
                    close(fd[0]); // закрываем дескриптор чтения из канала
                }
                // ждем окончания процессов потомков для обычного режима
                if (iLevel == 0 && !isBack) {
                    for (i = 0; i < iCount; i++) waitpid(PID[i], NULL, 0);
                }
            }
        }
    }
    if (iLevel == 1) free(PID);
}

// ----- Выполнение команд блоков
// LCmd - указатель на список блоков команд
void ExecLCmd(struct ListCmd *LCmd) {
    int *iPID = NULL;
    while (LCmd != NULL) {
        if (LCmd->BCmd->iNumWords != 0) ExecBCmdPipe(LCmd->BCmd, LCmd->isBack, 0, iPID, 0); // выполнение команд блока
        LCmd = LCmd->Next; // смещаемся к следующему блоку команд
    }
}

// ----- Очистка команд
// LCmd - текущий указатель на список блоков команд
struct ListCmd *ClearListCmd(struct ListCmd *LCmd) {
    struct ListCmd *LCmdTmp;

    struct BlockCmd *BCmd, *BCmdTmp; // указатели на команды блока
    char **Words; // указатель на массив слов в команде
    int i = 0;

    while (LCmd != NULL) {
        BCmd = LCmd->BCmd; // указатель на первую блочную команду
        while (BCmd != NULL) {
            Words = BCmd->Words;
            i = 0;
            while (Words[i] != NULL) free(Words[i++]); // удаляем слова команды
            if (BCmd->InputCmd) free(BCmd->InputCmd);
            if (BCmd->OutputCmd) free(BCmd->OutputCmd);
            BCmdTmp = BCmd; // сохраняем указатель на удаляемую команду
            BCmd = BCmd->Next; // переходим к следующей команде
            free(BCmdTmp); // удаляем команду
        }
        LCmdTmp = LCmd; // сохраняем указатель на удаляемый блок
        LCmd = LCmd->Next; // переходим к следующему блоку
        free(LCmdTmp); // удаляем блок
    }

    // создаем новый пустой блок команд
    LCmd = (struct ListCmd *) malloc(sizeof(struct ListCmd));
    LCmd->Next = NULL; LCmd->BCmd = NULL; LCmd->isBack = 0;

    // создаем новую пустую команду
    BCmd = (struct BlockCmd *) malloc(sizeof(struct BlockCmd));
    BCmd->Next = NULL;
    BCmd->Words = NULL;
    BCmd->iNumWords = 0;
    BCmd->InputCmd = NULL;
    BCmd->OutputCmd = NULL;
    BCmd->isAdd = 0;

    // устанавливаем указатель в блоке на эту команду
    LCmd->BCmd = BCmd;

    // возвращаем указатель на новый пустой блок команд
    return LCmd;

}

// -----
int main() {
    int fd = open("../Tmp", O_RDONLY);
    if (fd == -1) {
        perror("can't open file");
        return 1;
    }
    dup2(fd, 0);
    close(fd);

    // создаем новый пустой блок команд
    struct ListCmd *LCmd = (struct ListCmd *) malloc(sizeof(struct ListCmd));
    struct ListCmd *FLCmd = LCmd; // указатель на начало списка блоков команд

    int iEnd = 0; // 1 - конец ввода команды, 0 - в противном случае
    int iExit = 0; // 1 - конец работы Shell, 0 - в противном случае
    int iAddList = 0; // 1 - если нужно добавить новый блок команд, 0 - в противном случае
    char *Word = NULL;

    LCmd->Next = NULL; LCmd->BCmd = NULL; LCmd->isBack = 0;

    struct BlockCmd *BCmd = (struct BlockCmd *) malloc(sizeof(struct BlockCmd));
    // создаем новую пустую команду
    BCmd->Next = NULL;
    BCmd->Words = NULL;
    BCmd->iNumWords = 0;
    BCmd->InputCmd = NULL;
    BCmd->OutputCmd = NULL;
    BCmd->isAdd = 0;

    // устанавливаем указатель в блоке на эту команду
    LCmd->BCmd = BCmd;

    while (!iExit){
        //while (waitpid(-1, NULL, WNOHANG) > 0);

        PrintWelcome(); // печать приветствия
        // считывание команды
        while (!iEnd) {
            Word = GetWord(&iEnd);
            switch (WhatIsWord(Word)) {
                case '&'  : iAddList = 1; break;
                case '|'  : BCmd = AddBlockCmd(BCmd); break;
                case '<'  : Word = GetWord(&iEnd); BCmd->InputCmd = Word; break;
                case '>'  : Word = GetWord(&iEnd); BCmd->OutputCmd = Word; break;
                case 'A'  : Word = GetWord(&iEnd); BCmd->OutputCmd = Word; BCmd->isAdd = 1; break;
                default   : if (strlen(Word) > 0) {
                    if (iAddList) {
                        // создаем новый блок команд и добавляем в него команду
                        iAddList = 0;
                        LCmd = AddListCmd(LCmd);
                        BCmd = LCmd->BCmd;
                        BCmd->Words = AddWordToBlockCmd(BCmd, Word);
                    }
                    else {
                        // добавляем команду в текущий блок команд
                        BCmd->Words = AddWordToBlockCmd(BCmd, Word);
                    }
                }
                        break;
            }
        }
        iEnd = 0; // обнуление статуса ввода команды

        // проверяем введена ли хотя бы одна команда
        if (LCmd->BCmd->iNumWords != 0) {
            // PrintCommands(FLCmd); // печать команды
            if (strcmp(LCmd->BCmd->Words[0],"exit") == 0) iExit = 1;
            else {
                // выполнение команды, если не exit
                iAddList = 0;
                ExecLCmd(FLCmd);
                // очистка команды
                FLCmd = ClearListCmd(FLCmd);
                LCmd = FLCmd;
            }
        }
    }
    return 0;
}