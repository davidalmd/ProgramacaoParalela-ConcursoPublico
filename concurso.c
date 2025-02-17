#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CANDIDATOS 200
#define MAX_QUESTOES 30
#define CODIGO_PROVA 303
#define GD_MIN 4.0 // Grau de dificuldade mínimo
#define NOTA_CORTE_REMANEJAMENTO 70.0 // Nota mínima para remanejamento

typedef struct {
    int matricula;
    int codigo_prova;
    char respostas[MAX_QUESTOES];
} Candidato;

typedef struct {
    int matricula;
    double nota;
} Resultado;

void ler_gabarito(char *gabarito, const char *arquivo) {
    FILE *file = fopen(arquivo, "r");
    if (!file) {
        printf("Erro ao abrir o gabarito.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    for (int i = 0; i < MAX_QUESTOES; i++) {
        fscanf(file, " %c,", &gabarito[i]);
    }
    
    fclose(file);
}

int ler_candidatos(Candidato *candidatos, const char *arquivo) {
    FILE *file = fopen(arquivo, "r");
    if (!file) {
        printf("Erro ao abrir o arquivo de respostas.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    int count = 0;
    while (count < MAX_CANDIDATOS && fscanf(file, "%d,%d,", &candidatos[count].matricula, &candidatos[count].codigo_prova) != EOF) {
        if (candidatos[count].codigo_prova == CODIGO_PROVA) {
            for (int i = 0; i < MAX_QUESTOES; i++) {
                fscanf(file, " %c,", &candidatos[count].respostas[i]);
            }
            count++;
        } else {
            char lixo[100]; // Descarta a linha dos candidatos com outro código
            fgets(lixo, sizeof(lixo), file);
        }
    }
    
    fclose(file);
    return count;
}

void calcular_graus_dificuldade(Candidato *candidatos, int total_candidatos, char *gabarito, double *gd) {
    int acertos[MAX_QUESTOES] = {0};
    
    // Contar quantos acertaram cada questão
    for (int i = 0; i < total_candidatos; i++) {
        for (int j = 0; j < MAX_QUESTOES; j++) {
            if (candidatos[i].respostas[j] == gabarito[j]) {
                acertos[j]++;
            }
        }
    }

    // Encontrar a questão com maior percentual de acertos
    int max_acertos = 0;
    for (int j = 0; j < MAX_QUESTOES; j++) {
        if (acertos[j] > max_acertos) {
            max_acertos = acertos[j];
        }
    }

    // Calcular o Grau de Dificuldade de cada questão
    for (int j = 0; j < MAX_QUESTOES; j++) {
        if (acertos[j] > 0) {
            gd[j] = ((double)max_acertos / acertos[j]) * GD_MIN;
        } else {
            gd[j] = 10.0; // Se ninguém acertou, assume-se um grau de dificuldade alto
        }
    }
}

void calcular_pontuacoes(double *gd, double *pontuacoes) {
    double soma_gd[3] = {0.0}; // Somatório de GD para cada matéria

    // Somar GDs por grupo
    for (int j = 0; j < MAX_QUESTOES; j++) {
        if (j < 10) soma_gd[0] += gd[j];
        else if (j < 20) soma_gd[1] += gd[j];
        else soma_gd[2] += gd[j];
    }

    // Calcular pontuação por questão
    for (int j = 0; j < MAX_QUESTOES; j++) {
        if (j < 10) pontuacoes[j] = (gd[j] / soma_gd[0]) * 100;
        else if (j < 20) pontuacoes[j] = (gd[j] / soma_gd[1]) * 100;
        else pontuacoes[j] = (gd[j] / soma_gd[2]) * 100;
    }
}

double calcular_nota(char *respostas, char *gabarito, double *pontuacoes) {
    double nota = 0.0;
    for (int i = 0; i < MAX_QUESTOES; i++) {
        if (respostas[i] == gabarito[i]) {
            nota += pontuacoes[i];
        }
    }
    return nota;
}

void ordenar_resultados(Resultado *resultados, int total) {
    for (int i = 0; i < total - 1; i++) {
        for (int j = i + 1; j < total; j++) {
            if (resultados[i].nota < resultados[j].nota) {
                Resultado temp = resultados[i];
                resultados[i] = resultados[j];
                resultados[j] = temp;
            }
        }
    }
}

void salvar_arquivo(const char *nome_arquivo, Resultado *resultados, int total) {
    FILE *file = fopen(nome_arquivo, "w");
    if (!file) {
        printf("Erro ao criar o arquivo %s\n", nome_arquivo);
        return;
    }
    fprintf(file, "Posição,Código,Matricula,Nota\n");
    for (int i = 0; i < total; i++) {
        fprintf(file, "%d,%d,%d,%.1f\n", i + 1, CODIGO_PROVA, resultados[i].matricula, resultados[i].nota);
    }
    fclose(file);
}

int main(int argc, char *argv[]) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char gabarito[MAX_QUESTOES];
    Candidato candidatos[MAX_CANDIDATOS];
    int total_candidatos = 0;
    
    if (rank == 0) {
        // Leitura inicial no processo mestre
        ler_gabarito(gabarito, "./gabarito.csv");
        total_candidatos = ler_candidatos(candidatos, "./respostas.csv");
    }

    // Distribuir dados
    MPI_Bcast(&total_candidatos, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(gabarito, MAX_QUESTOES, MPI_CHAR, 0, MPI_COMM_WORLD);

    double gd[MAX_QUESTOES], pontuacoes[MAX_QUESTOES];
    
    if (rank == 0) {
        calcular_graus_dificuldade(candidatos, total_candidatos, gabarito, gd);
        calcular_pontuacoes(gd, pontuacoes);
    }
    
    MPI_Bcast(gd, MAX_QUESTOES, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(pontuacoes, MAX_QUESTOES, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    int local_count = total_candidatos / size;
    if (rank < total_candidatos % size) local_count++;

    Candidato *meus_candidatos = malloc(local_count * sizeof(Candidato));
    MPI_Scatter(candidatos, local_count * sizeof(Candidato), MPI_BYTE, 
                meus_candidatos, local_count * sizeof(Candidato), MPI_BYTE, 
                0, MPI_COMM_WORLD);

    double notas[local_count];
    for (int i = 0; i < local_count; i++) {
        notas[i] = calcular_nota(meus_candidatos[i].respostas, gabarito, pontuacoes);
    }

    Resultado *resultados_locais = malloc(local_count * sizeof(Resultado));
    for (int i = 0; i < local_count; i++) {
        resultados_locais[i].matricula = meus_candidatos[i].matricula;
        resultados_locais[i].nota = notas[i];
    }

    Resultado *resultados_finais = NULL;
    if (rank == 0) {
        resultados_finais = malloc(total_candidatos * sizeof(Resultado));
    }

    MPI_Gather(resultados_locais, local_count * sizeof(Resultado), MPI_BYTE,
               resultados_finais, local_count * sizeof(Resultado), MPI_BYTE,
               0, MPI_COMM_WORLD);

    if (rank == 0) {
        ordenar_resultados(resultados_finais, total_candidatos);
        
        // Criar listas para classificados e remanejamento
        int classificados_count = total_candidatos < 10 ? total_candidatos : 10;
        int remanejamento_count = 0;
        Resultado *remanejamento = malloc(total_candidatos * sizeof(Resultado));

        for (int i = 10; i < total_candidatos; i++) {
            if (resultados_finais[i].nota >= NOTA_CORTE_REMANEJAMENTO) {
                remanejamento[remanejamento_count++] = resultados_finais[i];
            }
        }

        // Salvar os arquivos
        salvar_arquivo("./resultados/classificados.csv", resultados_finais, classificados_count);
        salvar_arquivo("./resultados/remanejamento.csv", remanejamento, remanejamento_count);
        salvar_arquivo("./resultados/resultado_geral.csv", resultados_finais, total_candidatos);

        // Print final
        for (int i = 0; i < total_candidatos; i++) {
            printf("%d - Código %d | %d | Nota: %.1f\n",
                   i + 1, CODIGO_PROVA, resultados_finais[i].matricula, resultados_finais[i].nota);
        }

        // Liberar memória
        free(resultados_finais);
        free(remanejamento);
    }

    free(meus_candidatos);
    free(resultados_locais);
    MPI_Finalize();
    return 0;
}