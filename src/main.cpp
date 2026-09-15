#include <Arduino.h>
#include <Stepper.h>

// =====================================================
// CONFIGURAÇÃO DO MOTOR
// =====================================================

const int STEPS_PER_REVOLUTION = 2068;

// Quantidade que estamos testando
const int STEPS_PER_COMPARTMENT = 274;

// Velocidade do motor
// Vamos começar BEM devagar para reduzir a chance
// de perda de passos.
const int MOTOR_SPEED = 3;

// =====================================================
// PINOS
// =====================================================

#define IN1 5
#define IN2 18
#define IN3 19
#define IN4 21

#define BUTTON_PIN 4

// =====================================================
// MOTOR
// =====================================================

Stepper motor(
    STEPS_PER_REVOLUTION,
    IN1,
    IN3,
    IN2,
    IN4
);

// =====================================================
// CONTROLE
// =====================================================

int movimento = 0;

bool ultimoEstadoBotao = HIGH;

unsigned long ultimoClique = 0;

const unsigned long DEBOUNCE = 250;


// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    motor.setSpeed(MOTOR_SPEED);

    Serial.println();
    Serial.println("=================================");
    Serial.println(" TESTE MECANICO DO ELOS");
    Serial.println("=================================");
    Serial.println();

    Serial.println("Motor: 28BYJ-48");
    Serial.println("Passos por movimento: 254");
    Serial.println("Velocidade: 3 RPM");
    Serial.println();

    Serial.println("Pressione o botao para mover.");
    Serial.println();

    Serial.print("Posicao inicial: ");
    Serial.println(movimento);
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
    bool estadoBotao = digitalRead(BUTTON_PIN);

    // Detecta apenas o momento em que o botão
    // é pressionado.
    if (estadoBotao == LOW && ultimoEstadoBotao == HIGH)
    {
        // Debounce
        if (millis() - ultimoClique > DEBOUNCE)
        {
            ultimoClique = millis();

            movimento++;

            Serial.println();
            Serial.println("---------------------------------");

            Serial.print("Movimento #");
            Serial.println(movimento);

            Serial.print("Executando ");
            Serial.print(STEPS_PER_COMPARTMENT);
            Serial.println(" passos...");

            // =================================================
            // MOVIMENTO
            // =================================================

            motor.step(STEPS_PER_COMPARTMENT);

            // =================================================
            // RESULTADO
            // =================================================

            Serial.println("Movimento concluido.");

            Serial.print("Total de passos teoricos: ");
            Serial.println(
                movimento * STEPS_PER_COMPARTMENT
            );

            Serial.println("---------------------------------");
        }
    }

    ultimoEstadoBotao = estadoBotao;
}