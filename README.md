# Porta-retratos ESP32-P4 — JC1060P470C_I

Firmware em desenvolvimento para a placa JC1060P470C_I.

## Objetivo
- Inicialização do display 1024x600 pelo BSP específico da placa.
- Leitura do cartão microSD pelo SDMMC do BSP.
- Fotos JPEG na pasta `/FOTOS`.
- Slideshow automático, mantendo a proporção das fotografias.

## Compilação automática
O GitHub Actions baixa o projeto oficial de referência da placa, aplica o firmware deste repositório e compila com ESP-IDF 5.4.2 para ESP32-P4.

O primeiro build é um teste de hardware: tela, backlight e montagem do microSD. A exibição JPEG/slideshow será integrada depois que esse primeiro build for validado.

Abra a aba **Actions**, selecione **Build firmware ESP32-P4** e execute **Run workflow**. Os arquivos de compilação aparecem em **Artifacts** quando a execução terminar com sucesso.

**Atenção:** ainda não grave os arquivos até recebermos uma compilação concluída e conferirmos os endereços em `flasher_args.json`.
