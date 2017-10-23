#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

/* 

System calls utilizadas:
	sbrk() move o break ao dado incremento em bytes e retorna
o novo endereço do break.
	Sua funcionalidade varia conforme o argumento passado como parametro,
temos como exemplo as chamadas:

sbrk(0) retorna o endereço atual do break.
sbrk(x) com um valor positivo move o break e incrementa a heap em x bytes.
sbrk(-x) com um valor negativo decrementa a heap em x bytes.



A biblioteca é compilada com o comando
gcc -o dkalloc2.so -fPIC -shared dkalloc2.c

depois é setado a variável de ambiente LD_PRELOAD para utilizar a biblioteca dkalloc
export LD_PRELOAD=$PWD/dkalloc2.so
compila o programa de teste

gcc -Wall teste.c -o teste
executa o programa de teste
./teste

*/

int multiplo(int x){
	int ret;
	ret = ((((x - 1) >> 2) << 2) + 4);
	return ret;
}


/* cabeçalho do bloco,
contendo o tamanho do bloco,
uma variavel para verificar se esta livre ou não,
e um ponteiro para o proximo bloco da lista */

struct cabecalho {
	size_t tamanho;
	unsigned livre;
	struct cabecalho *prox;
};

/* ponteiros para a cabeça e calda da lista respectivamente */
struct cabecalho *head, *tail;

/* para previnir duas ou mais threads acessem a memoria
concorrentemente vamos utilizar um mutex */ 
pthread_mutex_t bloqueio_desbloqueio;



struct cabecalho *pega_bloco_livre(size_t tamanho)
{
	/* começa a varredura no começo da lista */
	struct cabecalho *bloco = head;
	/* enquanto é o bloco não é nulo */
	while(bloco) {
		/* se o bloco está livre e possui o tamanho maior que o desejado 
		retornamos esse bloco para ser utilizado */
		if (bloco->livre && bloco->tamanho >= tamanho)
			return bloco;
		/* caso contrario continuamos percorrendo */
		bloco = bloco->prox;
	}
	/* caso não haja nenhum bloco livre do tamanho que desejamos é retornado nulo */
	return NULL;
}


void *malloc(size_t tamanho)
{
	size_t tamanho_total;
	void *bloco;
	struct cabecalho *header;
	/* caso tamanho seja nulo ou 0 */

	/* utilizamos o mutex para garantir que apenas uma thread esteja executando essa região do código 
	devemos garantir que 2 thread não tentem acessar a mesma região de memória. */
	pthread_mutex_lock(&bloqueio_desbloqueio);

	size_t t = multiplo(tamanho);

	/* procura um bloco livre maior que o tamanho passado como parametro para o malloc */
	header = pega_bloco_livre(t);
	/* caso encontrado um bloco disponível */
	if (header) {
		/* marcamos o bloco como sendo utilizado */
		header->livre = 0;
		/*  desbloqueia o mutex */
		pthread_mutex_unlock(&bloqueio_desbloqueio);
		/* utilizamos (header + 1) para o retorna o endereço do primeiro byte ao final do header,
		ou seja, a área em que podemos escrever no bloco */

		return (void*)(header + 1);
	}
	/* caso não exista bloco disponível com o tamanho que precisamos,
	então devemos criar um novo bloco, o tamanho total do novo bloco
	é a soma do tamanho do cabecalho e o tamanho passado como argumento para o malloc */
	tamanho_total = sizeof(struct cabecalho) + t;
	/* sbrk incrimenta o break, aumentando a heap no tamanho_total e retorna a posição do break(final do bloco) */
	bloco = sbrk(tamanho_total);
	/* caso sbrk falhe retornara -1 */
	if (bloco == (void*) -1) {
		/* desbloqueia o mutex */
		pthread_mutex_unlock(&bloqueio_desbloqueio);
		return NULL;
	}
	/* cabeçalho aponta para o break 
	e atualizamos suas informações de acordo com o novo bloco*/
	header = bloco;
	header->tamanho = t;
	header->livre = 0;
	header->prox = NULL;
	/* se a lista estava vazia, atualizamos o primeiro elemento como sendo o novo bloco */
	if (!head)
		head = header;
	/* atualizamos o ponteiro de prox do ultimo bloco na fila, para agora
	apontar ao seu novo sucessor */
	if (tail)
		tail->prox = header;
	/* sempre o novo bloco alocado estará na calda */
	tail = header;
	/* desbloqueia o mutex */
	pthread_mutex_unlock(&bloqueio_desbloqueio);
	/* retorno do primeiro byte ao final do header */
	return (void*)(header + 1);
}


void free(void *bloco)
{
	struct cabecalho *header, *tmp;
	/* ponteiro que utilizaremos para determinar a posicao do breaker */
	void *programbreak;

	/* caso bloco a ser lbierado seja nulo,
	encerramos a funcao */
	if (!bloco)
		return;
	/* bloqueio do mutex */
	pthread_mutex_lock(&bloqueio_desbloqueio);
	/* utilizamos bloco-1 para acessar a posicao de memoria onde esta o header */
	header = (struct cabecalho*)bloco - 1;

	/* obtemos a atual posicao do header com sbrk */
	programbreak = sbrk(0);
	/* verifica se é o ultimo bloco da lista */
	if ((char*)bloco + header->tamanho == programbreak) {
		/* se é o unico bloco da lista
		então colocamos as variaveis head e tail como nulas */
		if (head == tail) {
			head = tail = NULL;
		} else {
			/* caso não seja o unico bloco da lista,  */
			tmp = head;
			/* percorre a lista */
			while (tmp) {
				/* caso o proximo elemento da lista seja a calda, marcamos o ponteiro
				de seu antecessor como nulo, e a lista diminui em 1 bloco */
				if(tmp->prox == tail) {
					tmp->prox = NULL;
					tail = tmp;
				}
				tmp = tmp->prox;
			}
		}
		/* decrementa a heap no tamanho do cabeçalho e do bloco */
		sbrk(0 - sizeof(struct cabecalho) - header->tamanho);
		/* desbloqueio mutex */
		pthread_mutex_unlock(&bloqueio_desbloqueio);
		return;
	}
	/* caso não seja o ultimo bloco da lista, apenas marca a posicao como livre a ser utilizada novamente */
	header->livre = 1;
	/* desbloqueio mutex */
	pthread_mutex_unlock(&bloqueio_desbloqueio);
}