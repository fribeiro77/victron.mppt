#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h> // Importa a Biblioteca PubSubClient
#include <Wire.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

#define VERSAO 2.25

#define D0    16
#define D1    5
#define D2    4 //ce
#define D3    0 // nao usar
#define D4    2 // nao usar
#define D5    14
#define D6    12
#define D7    13
#define D8    15 //cs - nao usar
#define D9    3
#define D10   1

// Serial variables
#define rxPin D7
#define txPin D6                                    // TX Not used
SoftwareSerial victronSerial(rxPin, txPin);         // RX, TX Using Software Serial so we can use the hardware serial to check the ouput
                                                    // via the USB serial provided by the NodeMCU.
unsigned int posfa=0;
byte checksumNano = 0;
String tabela_Html;

String leitura;
byte last_checksumNano = 0;

#define TOPICO_PUBLISH   "/nodemcu/mppt"    //tópico MQTT de envio de informações para Broker
#define TOPICO_STATUS   "/nodemcu/status"    //tópico MQTT de envio de informações para Broker
#define TOPICO_ERRO                   "/nodemcu/xmppt_erro"    //tópico MQTT de envio de informações para Broker
#define ID_MQTT  "v6_Castelo_mppt"     //id mqtt (para identificação de sessão)

// WIFI
const char* ssid = "************"; // ssid / nome da rede WI-FI que deseja se conectar
const char* password = "************"; // Senha da rede WI-FI que deseja se conectar

IPAddress ip(192,168,1,248); //COLOQUE UMA FAIXA DE IP DISPONÍVEL DO SEU ROTEADOR. EX: 192.168.1.110 **** ISSO VARIA, NO MEU CASO É: 192.168.0.175
IPAddress gateway(192,168,1,10); //GATEWAY DE CONEXÃO (ALTERE PARA O GATEWAY DO SEU ROTEADOR)
IPAddress subnet(255,255,255,0); 

// MQTT
const char* BROKER_MQTT = "192.168.1.213"; //URL do broker MQTT que se deseja utilizar
int BROKER_PORT = 1883; // Porta do Broker MQTT

unsigned long ctrltempo;
unsigned long lastUpdateOk;
unsigned long lastUpdate;

unsigned int ordem=0;

boolean envia = false;

WiFiClient espClient; // Cria o objeto espClient
PubSubClient MQTT(espClient); // Instancia o Cliente MQTT passando o objeto espClient
ESP8266WebServer server(80);
 
void callback(char* topic, byte* payload, unsigned int length) 
{
  String meuTopico = String((char *)topic);
  String mensagem = String((char *)payload);
  /* Just in case */
}

void handleRoot() {
  tabela_Html = leitura;
  server.send(200, "text/html",
              "<html>" \
                "<head>" \
                  "<title>MPPT Victron</title></head>" \
                "<meta charset='utf-8' http-equiv='refresh' content='2' />" \
                "<body>" \
                  "<h2>Controlador de Carga | Victron BlueSolar 150/45!</h2>" \
                  "<h3>As últimas leituras são:</h3>" \
                  "<h4>Ordem: "+String(ordem)+"</h4>" \
                  "<h5>"+tabela_Html+"</h5>" \
                  "<h4>Versao: "+String(VERSAO)+"</h4>" \
                  "<h4>Ultima Leitura: "+String((int) ((millis()-lastUpdate)/1000))+" | Ultima Leitura Ok: "+String((int) ((millis()-lastUpdateOk)/1000))+"</h4>" \
                  "<h4>Meu CheckSum: "+String(last_checksumNano)+"</h4>" \
                  "<h4>Versao: "+String(VERSAO)+"</h4>" \
                  "<p></p>" \
                  "<p><a href=\"\\reboot\">Reiniciar Módulo</a></p>" \
                "</body>" \
              "</html>");
}

void handleReboot()
{
  VerificaConexoesWiFIEMQTT();
  server.send(200, "text/html",
              "<html>" \
                "<head><title>MPPT Victron</title></head>" \
                "<meta charset='utf-8' http-equiv='refresh' content='2' />" \
                "<meta charset='utf-8'>" \
                "<body>" \
                  "<h1>Reiniciando em 2 segundos</h1>" \
                  "<h2>Causado por leituras zeradas ou forçado!</h2>" \
                "</body>" \
              "</html>");

  delay(2000);      
  ESP.restart();
}

void reconectWiFi() 
{
    //se já está conectado a rede WI-FI, nada é feito. 
    //Caso contrário, são efetuadas tentativas de conexão
    if (WiFi.status() == WL_CONNECTED)
        return;
        
    WiFi.begin(ssid, password); // Conecta na rede WI-FI
    WiFi.config(ip, gateway, subnet);
    
    while (WiFi.status() != WL_CONNECTED) 
    {
        delay(100);
        Serial.print(".");
    }
  
    Serial.println();
    Serial.print("Conectado com sucesso na rede ");
    Serial.print(ssid);
    Serial.println("IP obtido: ");
    Serial.println(WiFi.localIP());
}

void initMQTT() 
{
    MQTT.setServer(BROKER_MQTT, BROKER_PORT);   //informa qual broker e porta deve ser conectado
    MQTT.setCallback(callback);

}

void reconnectMQTT() 
{
    while (!MQTT.connected()) 
    {
        if (!MQTT.connect(ID_MQTT,"mqtt","mqtt")) 
        {
        } 
        else 
        {
            delay(2000);
        }
    }
}

void VerificaConexoesWiFIEMQTT(void)
{
    reconectWiFi(); //se não há conexão com o WiFI, a conexão é refeita
    if (!MQTT.connected()) 
        reconnectMQTT(); //se não há conexão com o Broker, a conexão é refeita
}


void setup() {
    // Open serial communications and wait for port to open:
    Serial.begin(19200);
    victronSerial.begin(19200);

    WiFi.mode(WIFI_STA);
    reconectWiFi();
    initMQTT();

    // Porta padrao do ESP8266 para OTA eh 8266 - Voce pode mudar ser quiser, mas deixe indicado!
    // ArduinoOTA.setPort(8266);
  
    // O Hostname padrao eh esp8266-[ChipID], mas voce pode mudar com essa funcao
    ArduinoOTA.setHostname(ID_MQTT);
  
    // Nenhuma senha eh pedida, mas voce pode dar mais seguranca pedindo uma senha pra gravar
    // ArduinoOTA.setPassword((const char *)"123");
  
    ArduinoOTA.onStart([]() {
      Serial.println("Inicio...");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("nFim!");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progresso: %u%%r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Erro [%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Autenticacao Falhou");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Falha no Inicio");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Falha na Conexao");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Falha na Recepcao");
      else if (error == OTA_END_ERROR) Serial.println("Falha no Fim");
    });
    ArduinoOTA.begin();
    Serial.println("Pronto");
    Serial.print("Endereco IP: ");
    Serial.println(WiFi.localIP());
  
    server.on("/", handleRoot);
    server.on("/reboot", handleReboot);
    server.begin();
    Serial.println("Server Up");
 
    ctrltempo=millis();
    lastUpdate=millis();
    lastUpdateOk=millis();
}

void loop() {
    VerificaConexoesWiFIEMQTT();
    ArduinoOTA.handle(); //Allow wifi upload code
    server.handleClient();
    MQTT.loop();
    
    if (lastUpdateOk>millis()) lastUpdateOk=millis(); //Control for long running >48 days
    if (lastUpdate>millis()) lastUpdate=millis(); //Control for long running >48 days
    
    char fullReadOk[350];
    posfa=0;
    checksumNano=0;
    char rc;
    while (victronSerial.available() > 0) 
    {
      if (posfa>349) break; //Avoid infinite loop
      envia=true;
      rc = victronSerial.read();
      checksumNano = checksumNano + (byte) (rc);
      switch (rc)
      {
        case '\t': //Start Data
          fullReadOk[posfa++]='[';
          break;
        case '\0': 
          break;
        case '\n': //End Data
          fullReadOk[posfa++]=']';
          break;
        default: //Just printable data, exclude #
          if ((((byte) rc)>=65 && (byte) rc<=90) || (((byte) rc)>=97 && (byte) rc<=122) || (((byte) rc)>=48 && (byte) rc<=57))
          {
            fullReadOk[posfa++]=rc;
            lastUpdate=millis();
          }
          break;
      }
      yield();
      if ((posfa%2)==0) //Delay to improve good reads
        delay(1);
    }

    if (envia)
    {
      last_checksumNano=checksumNano;
      if (checksumNano==0) //True read with checksum ok
      {
        lastUpdateOk=millis();
      }
      envia_mqtt(String(fullReadOk), checksumNano); 
      envia=false;
    }
    yield(); //nice to have
}



void envia_mqtt(String fr, byte tt)  //Main publish
{
  String datax;
  char datax2[350];
  
  datax="Ordem";
    datax += "[" ;
    datax += String(++ordem) ;
    datax += "]l[0]lO[0" ; // My controls
    datax += fr ;
    datax += "0]qR[0]qt[0]V["; // Others controls
    datax.concat(String(VERSAO));
    datax.concat("]CSum[") ;
    datax.concat(String(tt)) ; //Send the last checksum 0=ok !=0 bad read
    datax.concat("]TmMCU[");
    datax.concat(String(millis())) ;
    datax.concat("] ") ;
  
  datax.toCharArray(datax2, datax.length());
  
  VerificaConexoesWiFIEMQTT();
  MQTT.publish(TOPICO_PUBLISH, datax2);
  leitura=fr + String('\0'); //Show on html
}

void send_mqtt(String fr)  //Generic publish
{
  String datax;
  char datax2[100];
  
  //ordem = ordem + 1L;
  datax="Ordem";
    datax += "[0]String[" ;
    datax += fr ;
    datax += "] " ;
  
  datax.toCharArray(datax2, datax.length());
  
  VerificaConexoesWiFIEMQTT();
  MQTT.publish(TOPICO_STATUS, datax2);
}
