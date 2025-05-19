#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ssd1306_i2c.h"
#include "hardware/dma.h"
#include "include/ssd1306.h"
#include <time.h>

const uint I2C_SDA = 14;
const uint I2C_SCL = 15;

int dma_channel;
dma_channel_config dma_config;

void render_on_display_dma(const uint8_t *data, size_t length) {
    dma_channel_set_read_addr(dma_channel, data, false);
    dma_channel_set_trans_count(dma_channel, length, true);
    dma_channel_wait_for_finish_blocking(dma_channel);
}

void setup_dma() {
    dma_channel = dma_claim_unused_channel(true);
    dma_config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
    channel_config_set_dreq(&dma_config, DREQ_I2C1_TX);
    dma_channel_configure(
        dma_channel,
        &dma_config,
        &i2c1_hw->data_cmd,
        NULL,
        0,
        false
    );
}

void set_pixel(uint8_t *buffer, int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    int byte_index = (y / 8) * 128 + x;
    buffer[byte_index] |= (1 << (y % 8));
}

int pixel_esta_ligado(uint8_t *buffer, int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64)
        return 0;  // fora da tela, considera desligado

    int byte_index = (y / 8) * 128 + x;
    int bit_mask = 1 << (y % 8);

    return (buffer[byte_index] & bit_mask) != 0;
}

void desenha_bolinha(uint8_t *buffer, int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    buffer[(y / 8) * 128 + x] |= (1 << (y % 8));
}

void limpa_bolinha(uint8_t *buffer, int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    buffer[(y / 8) * 128 + x] &= ~(1 << (y % 8));
}

#define NUM_SLOTS 5
int slot_counts[NUM_SLOTS] = {0};
const int HISTOGRAM_WIDTH = 20;  // Largura do histograma
const int HISTOGRAM_START_X = 105;  // Posição X inicial (lado direito)
const int HISTOGRAM_START_Y = 10;   // Posição Y inicial (topo)
const int MAX_BAR_HEIGHT = 40;      // Altura máxima das barras

// Estrutura para representar uma bolinha
typedef struct {
    int x;
    int y;
    int slot_index;
    int nivel;
    bool ativa;
    bool descendo;
    uint32_t ultimo_update;
    uint32_t intervalo;
} Bolinha;

#define MAX_BOLINHAS 5
Bolinha bolinhas[MAX_BOLINHAS];

void inicializa_bolinhas() {
    for (int i = 0; i < MAX_BOLINHAS; i++) {
        bolinhas[i].ativa = false;
    }
}

void desenha_histograma(uint8_t *buffer) {
    // Limpa a área do histograma (lado direito)
    for (int x = HISTOGRAM_START_X; x < 128; x++) {
        for (int y = HISTOGRAM_START_Y; y < 64; y++) {
            limpa_bolinha(buffer, x, y);
        }
    }
    
    // Encontra o valor máximo para escalar o histograma
    int max_count = 1;
    for (int i = 0; i < NUM_SLOTS; i++) {
        if (slot_counts[i] > max_count) {
            max_count = slot_counts[i];
        }
    }
    
    // Desenha as barras do histograma verticalmente
    for (int i = 0; i < NUM_SLOTS; i++) {
        int bar_height = (slot_counts[i] * MAX_BAR_HEIGHT) / max_count;
        if (bar_height > MAX_BAR_HEIGHT) bar_height = MAX_BAR_HEIGHT;
        
        int slot_x = HISTOGRAM_START_X + (i * 3);  // Espaço entre as barras
        
        for (int h = 0; h < bar_height; h++) {
            int y_pos = HISTOGRAM_START_Y + MAX_BAR_HEIGHT - h;
            set_pixel(buffer, slot_x, y_pos);
            set_pixel(buffer, slot_x + 1, y_pos);  // Barras com 2 pixels de largura
        }
        
        // Desenha a base do histograma
        set_pixel(buffer, slot_x, HISTOGRAM_START_Y + MAX_BAR_HEIGHT + 1);
        set_pixel(buffer, slot_x + 1, HISTOGRAM_START_Y + MAX_BAR_HEIGHT + 1);
    }
}

void adiciona_bolinha() {
    for (int i = 0; i < MAX_BOLINHAS; i++) {
        if (!bolinhas[i].ativa) {
            bolinhas[i].x = 4;
            bolinhas[i].y = 32;
            bolinhas[i].slot_index = 0;
            bolinhas[i].nivel = 0;
            bolinhas[i].ativa = true;
            bolinhas[i].descendo = false;
            bolinhas[i].ultimo_update = time_us_32();
            bolinhas[i].intervalo = 150000; // 150ms em microssegundos
            return;
        }
    }
}

void atualiza_bolinhas(uint8_t *buffer, struct render_area *area) {
    uint32_t agora = time_us_32();
    const int levels = 5;
    const int spacing_x = 18;
    const int spacing_y = 8;
    const int center_y = 32;
    const int start_x = 4;
    const int final_x = start_x + levels * spacing_x + 2; // Posição X final
    
    for (int i = 0; i < MAX_BOLINHAS; i++) {
        if (!bolinhas[i].ativa) continue;
        
        if (agora - bolinhas[i].ultimo_update >= bolinhas[i].intervalo) {
            bolinhas[i].ultimo_update = agora;
            
            // Limpa a bolinha da posição atual
            limpa_bolinha(buffer, bolinhas[i].x, bolinhas[i].y);
            
            if (bolinhas[i].nivel < levels) {
                // Fase de quicando nos pinos
                bolinhas[i].x = start_x + (bolinhas[i].nivel * spacing_x);
                
                int direcao = rand() % 2;
                if (direcao == 1) {
                    bolinhas[i].slot_index++;
                    bolinhas[i].y += spacing_y;
                } else {
                    bolinhas[i].y -= spacing_y;
                }
                
                bolinhas[i].nivel++;
                
                // Desenha na nova posição
                desenha_bolinha(buffer, bolinhas[i].x, bolinhas[i].y);
            } 
            else {
                // Posiciona no slot final
                bolinhas[i].x = final_x;
                bolinhas[i].y = center_y + (bolinhas[i].slot_index * spacing_y) - (levels * spacing_y / 2);
                
                // Pisca a bolinha antes de desaparecer
                desenha_bolinha(buffer, bolinhas[i].x, bolinhas[i].y);
                render_on_display(buffer, area);
                sleep_ms(100);
                limpa_bolinha(buffer, bolinhas[i].x, bolinhas[i].y);
                
                // Atualiza histograma
                if (bolinhas[i].slot_index >= 0 && bolinhas[i].slot_index < NUM_SLOTS) {
                    slot_counts[bolinhas[i].slot_index]++;
                    desenha_histograma(buffer);
                }
                
                bolinhas[i].ativa = false;
            }
        }
    }
}

void desenha_pinos(uint8_t *buffer) {
    int levels = 5;
    int spacing_x = 18;
    int spacing_y = 8;
    int center_y = 25;
    int start_x = 4;

    // Desenhar os pinos da Galton Board
    for (int col = 0; col < levels; col++) {
        int num_pinos = col + 1;
        int total_height = (num_pinos - 1) * spacing_y;
        int offset_y = center_y - total_height / 2;

        int x = start_x + col * spacing_x;

        for (int i = 0; i < num_pinos; i++) {
            int y = offset_y + i * spacing_y;
            set_pixel(buffer, x, y);
        }
    }

    // Desenhar os "buracos" finais
    int final_x = start_x + levels * spacing_x + 2;
    int num_slots = levels;
    int total_slot_height = (num_slots - 1) * spacing_y;
    int offset_y = center_y - total_slot_height / 2;

    for (int i = 0; i < num_slots; i++) {
        int y = offset_y + i * spacing_y;
        for (int j = 0; j < 4; j++) {
            set_pixel(buffer, final_x + j, y);
        }
    }
}

int main() {
    stdio_init_all();

    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_init();
    setup_dma();
    srand(time_us_32());

    struct render_area frame_area = {
        .start_column = 0,
        .end_column = ssd1306_width - 1,
        .start_page = 0,
        .end_page = ssd1306_n_pages - 1
    };
    calculate_render_area_buffer_length(&frame_area);

    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, sizeof(ssd));
    memset(slot_counts, 0, sizeof(slot_counts));
    
    inicializa_bolinhas();
    desenha_pinos(ssd);
    render_on_display(ssd, &frame_area);
    sleep_ms(1000);
    
    uint32_t ultima_bolinha = time_us_32();
    uint32_t intervalo_bolinhas = 500000; // 500ms entre bolinhas
    
    while (true) {
        uint32_t agora = time_us_32();
        
        // Adiciona nova bolinha periodicamente
        if (agora - ultima_bolinha >= intervalo_bolinhas) {
            adiciona_bolinha();
            ultima_bolinha = agora;
        }
        
        // Atualiza todas as bolinhas
        atualiza_bolinhas(ssd, &frame_area);
        
        // Renderiza a tela
        render_on_display(ssd, &frame_area);
        
        // Pequena pausa para não sobrecarregar
        sleep_ms(10);
    }
}