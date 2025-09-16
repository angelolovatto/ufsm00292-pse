/**
 * \file
 *
 * \brief Exemplos diversos de tarefas e funcionalidades de um sistema operacional multitarefas.
 *
 */

/**
 * \mainpage User Application template doxygen documentation
 *
 * \par Empty user application template
 *
 * Este arquivo contem exemplos diversos de tarefas e 
 * funcionalidades de um sistema operacional multitarefas.
 *
 *
...
 * Inclusao de arquivos de cabecalhos
 */
#include <asf.h>
#include "stdint.h"
#include "rtos.h"

/*
 * Prototipos das tarefas
 */
void tarefa_1(void);
void tarefa_2(void);
void tarefa_3(void);
void tarefa_4(void);
void tarefa_5(void);
void tarefa_6(void);
void tarefa_7(void);
void tarefa_8(void);
void tarefa_9(void);
void tarefa_10(void);

/*
 * Configuracao dos tamanhos das pilhas
 */
#define TAM_PILHA_1			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_2			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_3			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_4			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_5			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_6			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_7			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_8			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_9			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_10			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_OCIOSA	(TAM_MINIMO_PILHA + 24)

/*
 * Declaracao das pilhas das tarefas
 */
uint32_t PILHA_TAREFA_1[TAM_PILHA_1];
uint32_t PILHA_TAREFA_2[TAM_PILHA_2];
uint32_t PILHA_TAREFA_3[TAM_PILHA_3];
uint32_t PILHA_TAREFA_4[TAM_PILHA_4];
uint32_t PILHA_TAREFA_5[TAM_PILHA_5];
uint32_t PILHA_TAREFA_6[TAM_PILHA_6];
uint32_t PILHA_TAREFA_7[TAM_PILHA_7];
uint32_t PILHA_TAREFA_8[TAM_PILHA_8];
uint32_t PILHA_TAREFA_9[TAM_PILHA_9];
uint32_t PILHA_TAREFA_10[TAM_PILHA_10];
uint32_t PILHA_TAREFA_OCIOSA[TAM_PILHA_OCIOSA];

/*
 * Funcao principal de entrada do sistema
 */
int main(void)
{
    
#if 0
	system_init();
#endif
	
	/* Criacao das tarefas */
	/* Parametros: ponteiro, nome, ponteiro da pilha, tamanho da pilha, prioridade da tarefa */
    
	//CriaTarefa(tarefa_1, "Tarefa 1", PILHA_TAREFA_1, TAM_PILHA_1, 2);
	
	//CriaTarefa(tarefa_2, "Tarefa 2", PILHA_TAREFA_2, TAM_PILHA_2, 1);
	
    //CriaTarefa(tarefa_4,"Tarefa 4",PILHA_TAREFA_4,TAM_PILHA_4,2);
    
    CriaTarefa(tarefa_9,"Tarefa 9",PILHA_TAREFA_9,TAM_PILHA_9,3);
    CriaTarefa(tarefa_10,"Tarefa 10",PILHA_TAREFA_10,TAM_PILHA_10,2);
    
	/* Cria tarefa ociosa do sistema */
	CriaTarefa(tarefa_ociosa,"Tarefa ociosa", PILHA_TAREFA_OCIOSA, TAM_PILHA_OCIOSA, 0);
	
	/* Habilita interrupcoes globais no processador */
	sei();
	
	/* Inicia o escalonador */
	IniciaRTOS();
    
	/* O codigo nao devera alcancar este ponto */
	while(1);
}

/* Tarefas de exemplo que usam funcoes para ligar/desligar o LED de acordo com a alternancia entre as tarefas */
void tarefa_1(void)
{
	volatile uint16_t a = 0;
	for(;;)
	{
		a++;
		port_pin_set_output_level(LED_0_PIN, LED_0_ACTIVE); /* Liga LED. */
		TarefaContinua(2);
	
	}
}

void tarefa_2(void)
{
	volatile uint16_t b = 0;
	for(;;)
	{
		b++;
		TarefaSuspende(2);	
		port_pin_set_output_level(LED_0_PIN, !LED_0_ACTIVE); 	/* Turn LED off. */
	}
}

/* Tarefas de exemplo que usam funcoes para suspender as tarefas por algum tempo (atraso/delay) */
void tarefa_3(void)
{
	volatile uint16_t c = 0;
	for(;;)
	{
		c++;
		TarefaEspera(3); 	/* tarefa se coloca em espera por 3 marcas de tempo (ticks) */
	}
}

void tarefa_4(void)
{
	volatile uint16_t b = 0;
	for(;;)
	{
		b++;
		TarefaEspera(5);	/* tarefa se coloca em espera por 5 marcas de tempo (ticks) */
	}
}

/* Tarefas de exemplo que usam funcoes de semaforo */

semaforo_t SemaforoTeste = {0,0}; /* declaracao e inicializacao de um semaforo */

void tarefa_5(void)
{

	uint32_t a = 0;			/* inicializacoes para a tarefa */
	
	for(;;)
	{
		
		a++;				/* codigo exemplo da tarefa */

		TarefaEspera(3); 	/* tarefa se coloca em espera por 3 marcas de tempo (ticks) */
		
		SemaforoLibera(&SemaforoTeste); /* tarefa libera semaforo para tarefa que esta esperando-o */
		
	}
}

/* Exemplo de tarefa que usa semaforo */
void tarefa_6(void)
{
	
	uint32_t contador = 0;
	
	for(;;)
	{
		
		SemaforoAguarda(&SemaforoTeste);	/* tarefa espera ate alguem liberar o semaforo */
		
		contador++;							/* quando o semaforo for liberado ela executa */
		
		if(contador == 1)
		{
			SemaforoLibera(&SemaforoTeste);  /* se for a primeira vez, libera o semaforo mais uma vez */
			
			do
			{
				TarefaEspera(100);
			}
			while(contador);
		}
	}
}

/* Tarefas de produtor/consumidor usando semaforos */
#define TAM_BUFFER			16

uint8_t buffer[TAM_BUFFER];
semaforo_t SemaforoCheio = {0,0}; /* declaracao e inicializacao de um semaforo */
semaforo_t SemaforoVazio = {TAM_BUFFER,0}; /* declaracao e inicializacao de um semaforo */

void tarefa_7(void)
{

	uint8_t a = 1;			/* inicializacoes para a tarefa */
	uint8_t i = 0;
	
	for(;;)
	{
		SemaforoAguarda(&SemaforoVazio);
		
		buffer[i] = a++;
		i = (i+1) % TAM_BUFFER;
		
		SemaforoLibera(&SemaforoCheio);
		
	}
}

void tarefa_8(void)
{

	uint8_t valor, f = 0;			/* inicializacoes para a tarefa */
	uint8_t contador = 0;	
	
	for(;;)
	{
		do
		{
			REG_ATOMICA_INICIO();
			contador = SemaforoCheio.contador;
			REG_ATOMICA_FIM();
			
			if(!contador)
			{
				TarefaEspera(100);
			}
				
		} while (!contador);
		
		SemaforoAguarda(&SemaforoCheio);
		
		valor = buffer[f];
		f = (f+1) % TAM_BUFFER;		
		
		SemaforoLibera(&SemaforoVazio);
	}
}

void tarefa_9(void){

    volatile uint16_t cont=0;
    
    for(;;){
    
        cont++;
        TarefaEspera(500);
    }

}

/* NOVA TAREFA 10: pisca LED conforme ocupacao do buffer (SemaforoCheio) */
void tarefa_10(void)
{
    for (;;)
    {
        uint8_t ocupacao;

        /* Le ocupacao do buffer de forma atomica */
        REG_ATOMICA_INICIO();
            ocupacao = SemaforoCheio.contador;
        REG_ATOMICA_FIM();

        if (ocupacao >= (TAM_BUFFER / 2)) {
            /* Buffer "meio cheio" ou mais -> pisca rapido */
            port_pin_set_output_level(LED_0_PIN, LED_0_ACTIVE);
            TarefaEspera(100);
            port_pin_set_output_level(LED_0_PIN, !LED_0_ACTIVE);
            TarefaEspera(100);
        } else {
            /* Buffer vazio/baixo -> pisca lento */
            port_pin_set_output_level(LED_0_PIN, LED_0_ACTIVE);
            TarefaEspera(500);
            port_pin_set_output_level(LED_0_PIN, !LED_0_ACTIVE);
            TarefaEspera(500);
        }
    }
}

...
/* Tarefa ociosa do sistema */
void tarefa_ociosa(void)
{
	for(;;)
	{
		/* As CPUs modernas permitem habilitar o modo de baixo consumo (sleep/idle) apenas com uma instrucao.
		 * De forma alternativa para outros processadores, estes modos de baixo consumo podem ser habilitados
		 * com leitura de registradores de sistema/perifericos e instrucoes especificas de configuracao do clock.
		 * Com isto, caso a CPU tenha alguma tarefa para realizar ela "acorda", realiza a tarefa e em seguida
		 * volta para o modo de baixo consumo, economizando energia. */
		 //sleepmgr_enter_sleep();
	}
}
