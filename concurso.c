#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_QUESTOES 30
#define GD_M 4.0
#define CARGO_FILTRADO 303
#define MAX_CANDIDATOS 200

typedef struct {
    int inscricao;
    int cargo;
    char respostas[NUM_QUESTOES];
    double media_final;
} Candidato;

void carregar_gabarito(const char *arquivo, char *gabarito);
void carregar_respostas(const char *arquivo, Candidato *candidatos, int *total_candidatos);
void calcular_notas(Candidato *candidatos, int total_candidatos, char *gabarito, double *gd, double *pontuacoes);
double retornar_nota_final(char *respostas, char *gabarito, double *pontuacoes);
void salvar_classificados(const char *arquivo, Candidato candidatos[], int total_candidatos);

int main(int argc, char *argv[]) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    Candidato candidatos[MAX_CANDIDATOS];
    char gabarito[NUM_QUESTOES + 1];
    int total_candidatos = 0;

    if (rank == 0) {
        // Carregar os dados dos arquivos
        carregar_gabarito("./gabarito.csv", gabarito);
        carregar_respostas("./respostas.csv", candidatos, &total_candidatos);
    }

    // Broadcast do gabarito
    MPI_Bcast(&total_candidatos, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(gabarito, NUM_QUESTOES, MPI_CHAR, 0, MPI_COMM_WORLD);

    double gd[NUM_QUESTOES];
    double pontuacoes[NUM_QUESTOES];

    // Calcular notas
    if (rank == 0) {
        calcular_notas(candidatos, total_candidatos, gabarito, gd, pontuacoes);
    }

    MPI_Bcast(gd, NUM_QUESTOES, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(pontuacoes, NUM_QUESTOES, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    // Distribuir candidatos
    int *sendcounts = malloc(size * sizeof(int));
    int *displs = malloc(size * sizeof(int));

    int deslocamento = 0;
    for (int i = 0; i < size; i++) {
        sendcounts[i] = (total_candidatos / size) * sizeof(Candidato);
        if (i < total_candidatos % size) {
            sendcounts[i] += sizeof(Candidato);  // Distribui os "restantes"
        }
        displs[i] = deslocamento;
        deslocamento += sendcounts[i];
    }

    Candidato *meus_candidatos = malloc(sendcounts[rank]);
    MPI_Scatterv(candidatos, sendcounts, displs, MPI_BYTE,
                meus_candidatos, sendcounts[rank], MPI_BYTE,
                0, MPI_COMM_WORLD);

    // Calcular notas finais
    for (int i = 0; i < sendcounts[rank] / sizeof(Candidato); i++) {
        meus_candidatos[i].media_final = retornar_nota_final(meus_candidatos[i].respostas, gabarito, pontuacoes);
    }

    // Reunir resultados
    Candidato *candidatos_final = NULL;
    if (rank == 0) {
        candidatos_final = malloc(total_candidatos * sizeof(Candidato));
    }

    MPI_Gatherv(meus_candidatos, sendcounts[rank], MPI_BYTE,
               candidatos_final, sendcounts, displs, MPI_BYTE,
               0, MPI_COMM_WORLD);

    if (rank == 0) {
        for (int i = 0; i < total_candidatos; i++) {
            for (int j = i + 1; j < total_candidatos; j++) {
                if (candidatos_final[i].media_final < candidatos_final[j].media_final) {
                    Candidato aux = candidatos_final[i];
                    candidatos_final[i] = candidatos_final[j];
                    candidatos_final[j] = aux;
                }
            }
        }

        int classificados = total_candidatos < 30 ? total_candidatos : 30;

        // Salvar arquivos dos resultados
        salvar_classificados("./resultados/classificacao_oficial.csv", candidatos_final, classificados);
        salvar_classificados("./resultados/resultado_geral.csv", candidatos_final, total_candidatos);

        // Prints de resultados
        printf("Lista de Classificados:\n");
        printf("Obs.: Para ver o resultado geral, vá até o diretório de resultados e análise o arquivo específico.\n\n");
        printf("Col.| Inscrição | Média\n");
        for (int i = 0; i < classificados; i++) {
            printf("%d  |  %d  |  %.2f\n", i + 1, candidatos_final[i].inscricao, candidatos_final[i].media_final);
        }

        free(candidatos_final);
    }

    free(meus_candidatos);
    MPI_Finalize();
    return 0;
};

void carregar_gabarito(const char *arquivo, char *gabarito) {
    FILE *file = fopen(arquivo, "r");
    if (!file) {
        printf("Erro ao abrir %s\n", arquivo);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    for (int i = 0; i < NUM_QUESTOES; i++) {
        fscanf(file, " %c,", &gabarito[i]);
    }
    
    fclose(file);
};

void carregar_respostas(const char *arquivo, Candidato *candidatos, int *total_candidatos) {
    FILE *file = fopen(arquivo, "r");
    if (!file) {
        printf("Erro ao abrir %s\n", arquivo);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    while ((*total_candidatos) < MAX_CANDIDATOS && fscanf(file, "%d,%d,", &candidatos[*total_candidatos].inscricao, &candidatos[*total_candidatos].cargo) != EOF) {
        if (candidatos[*total_candidatos].cargo == CARGO_FILTRADO) {
            for (int i = 0; i < NUM_QUESTOES; i++) {
                fscanf(file, " %c,", &candidatos[*total_candidatos].respostas[i]);
            }
            (*total_candidatos)++;
        } else {
            char lixo[100];
            fgets(lixo, sizeof(lixo), file);
        }
    }

    fclose(file);
};

void calcular_notas(Candidato *candidatos, int total_candidatos, char *gabarito, double *gd, double *pontuacoes) {
    int acertos[NUM_QUESTOES] = {0};
    double soma_gd[3] = {0.0}; // Soma dos graus de dificuldade de cada área

    int max_acertos = 0;
    for (int i = 0; i < total_candidatos; i++) {
        for (int j = 0; j < NUM_QUESTOES; j++) {
            if (candidatos[i].respostas[j] == gabarito[j]) {
                acertos[j]++;
                if (acertos[j] > max_acertos) {
                    max_acertos = acertos[j];
                }
            }
        }
    }

    // Determinar pontuação de cada questão
    for (int j = 0; j < NUM_QUESTOES; j++) {
        if (acertos[j] > 0) {
            gd[j] = ((double)max_acertos / acertos[j]) * GD_M;
        } else {
            gd[j] = 12.0; // Questão sem acertos = GD alto
        }

        // Somar GDs por grupo de áreas
        if (j < 10) soma_gd[0] += gd[j];
        else if (j < 20) soma_gd[1] += gd[j];
        else soma_gd[2] += gd[j];
    }

    // Calcular pontuação por questão usando as fórmulas
    for (int j = 0; j < NUM_QUESTOES; j++) {
        if (j < 10) pontuacoes[j] = (gd[j] / soma_gd[0]) * 100;
        else if (j < 20) pontuacoes[j] = (gd[j] / soma_gd[1]) * 100;
        else pontuacoes[j] = (gd[j] / soma_gd[2]) * 100;
    }
};

double retornar_nota_final(char *respostas, char *gabarito, double *pontuacoes) {
    double nota = 0.0;
    for (int i = 0; i < NUM_QUESTOES; i++) {
        if (respostas[i] == gabarito[i]) {
            nota += pontuacoes[i];
        }
    }
    return nota;
};

void salvar_classificados(const char *arquivo, Candidato candidatos[], int total_candidatos) {
    FILE *file = fopen(arquivo, "w");
    if (!file) {
        printf("Erro ao criar %s\n", arquivo);
        exit(1);
    }
    fprintf(file, "Inscricao,Media\n");
    for (int i = 0; i < total_candidatos; i++) {
        fprintf(file, "%d,%.2f\n", candidatos[i].inscricao, candidatos[i].media_final);
    }
    fclose(file);
};
