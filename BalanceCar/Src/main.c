/*
* Note :
*   - Connecter:
*         STM32F1      MPU6050
*           PB6         SCL
*           PB7         SDA
*           PAB         INT
*   - user UART 
*         TX     PA9
*         RX     PA10
*/

/* Includes */
#include "main.h"
#include "stm32f1xx_hal.h"
#include "MPU6050.h"
#include "MPU6050_dmp_6axis_MotionApps20.h"

/*System values*/
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;
/*End system values*/

/*User values*/
// MPU6050 data value
uint32_t read = 0;
uint8_t devStatus;
uint8_t mpuIntStatus; 
DataMpu6050 MPU6050data;
bool dmpReady = false;
uint16_t packetSize;

Quaternion_t q;
VectorFloat_t gravity;
float ypr[3];

float Yaw,Pitch,Roll;
volatile bool mpuInterrupt = false;// indicates whether MPU interrupt pin has gone high

uint16_t fifoCount;
uint8_t fifoBuffer[64];
uint8_t buff_char[50];
// Moto values
int pwm = 0;
// Wait value
bool is_good_angle = false;
bool is_run = false;
float temp_pitch = 0;
float m_angle = 0;
uint8_t count_good = 0;


// PID controler value
double e = 0;
double integral = 0;
double error_prior = 0;
double iteration_time = 0.001;
double derivative = 0;
double output = 0;
float PID_bias = 0;
// PID value 6_30_0.2
double KP = 23;
double KI = 1;
double KD = 0;
// new pid
const float Kp = 26;
const float Ki = 0.2;
const float Kd = 1;
float pTerm, iTerm, dTerm, integrated_error, last_error, error;
const float K = 1;//1.9*1.12;
#define GUARD_GAIN 10.0

// Bluetooth values
uint8_t bufferData[10] = "";
uint8_t dataIndex = 0;
uint8_t receiveData = 0;
uint8_t dir = 0;
bool is_sent = false;  
float ControlAngle = 0;

/*End user values*/

/*System functions*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM2_Init(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
/*End System functions*/

/*User functions*/
void dmpDataReady(void);
void motoControl(double);
void SentPlotData(void);
void beep(int n);
void blink(int n);
//new pid
float constrain(float x, float a, float b);
void pid_calculator(void);
void RunMoto(void);
void StopMoto(void);
int speed;
/*End user functions*/


/***************************/
/******MAIN PROGRAM*********/
/***************************/
int main(void)
{
  // system Init
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  beep(1);

  /* UART (bluetooth) Init*/
  HAL_UART_Receive_IT(&huart1,&receiveData,1);
  
  /*DC Moto Init*/
  // Set up moto pin
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
  // enable in EN
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);

  /*MPU6050 Init*/
  MPU6050_Initialize(NT_MPU6050_Device_AD0_LOW,NT_MPU6050_Accelerometer_2G,NT_MPU6050_Gyroscope_2000s);
  MPU6050address(0xD0);
  MPU6050_initialize();
  GPIO_Init_IRQ(GPIOB,GPIO_PIN_0,EXTI0_IRQn);
  devStatus = MPU6050_dmpInitialize();
  MPU6050_setXGyroOffset(220);    // value set up only MPU6050 module
  MPU6050_setYGyroOffset(76);     // value set up only MPU6050 module
  MPU6050_setZGyroOffset(-85);    // value set up only MPU6050 module
  MPU6050_setZAccelOffset(1788);  // value set up only MPU6050 module
  MPU6050_setIntEnabled(0x12);
  if (devStatus == 0) {
    MPU6050_setDMPEnabled(true);
    mpuIntStatus = MPU6050_getIntStatus();
    dmpReady = true;
    packetSize = MPU6050_dmpGetFIFOPacketSize();
  }
  /*********************/
  /****loop system******/
  /*********************/
  while (1)
  {
    while (!mpuInterrupt && fifoCount <= packetSize){};
    mpuInterrupt = false;
    mpuIntStatus = MPU6050_getIntStatus();
    fifoCount = MPU6050_getFIFOCount();
    if ((mpuIntStatus & 0x10) || fifoCount == 1024){
      // reset so we can continue cleanly
      MPU6050_resetFIFO();
    }
    else if (mpuIntStatus & 0x02){
      // wait for correct available data length, should be a VERY short wait
      while (fifoCount < packetSize) fifoCount = MPU6050_getFIFOCount();

      // read a packet from FIFO
      MPU6050_getFIFOBytes(fifoBuffer, packetSize);

      // track FIFO count here in case there is > 1 packet available
      // (this lets us immediately read more without waiting for an interrupt)
      fifoCount -= packetSize;

      MPU6050_dmpGetQuaternion(&q, fifoBuffer);
      MPU6050_dmpGetGravity(&gravity, &q);
      MPU6050_dmpGetYawPitchRoll(ypr, &q, &gravity);
      Yaw=ypr[0]*180/3.14;
      Pitch=ypr[1]*180/3.14;
      Roll=ypr[2]*180/3.14;
      // Wait for good angle
      if (!is_run){
        if (!is_good_angle){
          if (temp_pitch - Pitch < 0.01 && Pitch - temp_pitch < 0.01)
            count_good++;
          else{
            temp_pitch = Pitch;
            count_good = 0;
          }
          if (count_good == 100){
            is_good_angle = true;
            //HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
            beep(2);
          }
        }
        else if (Pitch < 0.1 && Pitch > -0.1){
          is_run = true;
          HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
        }
      }
      // Calculator ouput of PID
      if (is_run){
        if (Pitch < 0.5 && Pitch > -0.5){
          StopMoto();
        }
        else{
          if (Pitch > -50 && Pitch < 50){
            pid_calculator();
            RunMoto();
          }
          else
            StopMoto();
        }
        if (ControlAngle == -3 || ControlAngle == 3) ControlAngle = 0;
      }
      //send data back
      if (is_sent) {
        SentPlotData();
        is_sent = false;
      }
    }
  }
}

/******************/
/* User functions */
/******************/

void dmpDataReady(void) {
    mpuInterrupt = true;
}

// Moto control
void motoControl(double PID_out){
  int temp = (int)PID_out;
  float angle_offset = 5;
  // set up direction for car
  if (m_angle > 0){
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
  }
  else {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  }
  // calculator pwm
  if (m_angle > angle_offset){
    pwm = temp;
  }
  else if (m_angle < -angle_offset){
    pwm = 255 + temp;
  }
  else if (m_angle > 0){
    pwm = temp * 3/ 4;
  }
  else {
    pwm = 255 + temp * 3/ 4;
  }
  // fix if over range
  if (pwm < 0) pwm = 0;
  if (pwm > 255) pwm = 255;
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pwm);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, pwm);
}

void pid_calculator(void){
  float CurrentAngle = Pitch + ControlAngle;
  error = CurrentAngle;
  pTerm = Kp * error;
  integrated_error += error*0.01;
  iTerm = Ki*constrain(integrated_error, -GUARD_GAIN, GUARD_GAIN);
  dTerm = Kd*(error - last_error);
  last_error = error;
  speed = constrain(K*(pTerm + iTerm + dTerm), -255, 255);
}
float constrain(float x, float a, float b){
  if (x <= a) return a;
  if (x >= b) return b;
  return x;
}

void RunMoto(void){
  if (speed > 0){
    if (dir == 3){
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    }
    else if (dir == 4){
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    }else {
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    }
  }
  else {
    speed = 255 + speed;
    if (dir == 3){
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    }
    else if (dir == 4){
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    }else {
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    }
  }
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, speed);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, speed);
}
void StopMoto(void){
  if (speed > 0){
    speed = speed + 105;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  }
  else {
    speed = 150 + speed;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
  }
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, speed);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, speed);
}

void SentPlotData(void){
  char sentString[30];
  sprintf(sentString,"%0.2f %0.2f %0.2f\n",Yaw,Pitch,Roll);
  HAL_UART_Transmit(&huart1,(uint8_t*)sentString,strlen(sentString),100);
}
void blink(int n){
  for (int i = 0; i < n; i++){
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    HAL_Delay(50);
  }
}
void beep(int n){
  for (int i = 0; i < n; i++){
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
    HAL_Delay(50);
  }
}
//Bluetooth received data to control
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
  if (huart->Instance == huart1.Instance){
    //Receive data
    //Check end symbol
      //When end symbol: Solve request
        //Send back data such as angle, gx, ax,...
        //Apply Setting data and send back answer OK or NOT
    if (receiveData == '\n'){
      /*
      * 0 : check OK function
      * 1 : request angle
      */
      switch (bufferData[0]){
        //Stop
        case '0':
          if (dir == 1)
            ControlAngle = -3;
          if (dir == 2) ControlAngle = 3;
          dir = 0;
          break;
        //Go up
        case '1':
          dir = 1;
          ControlAngle = 4;
          break;
        //Back down
        case '2':
          dir = 2;
          ControlAngle = -4;
          break;
        //Turn left
        case '3':
          dir = 3;
          break;
        //Turn right
        case '4':
          dir = 4;
          break;
        //request sent 
        case '5':
          is_sent = true;
          break;
      }
      dataIndex = 0;
    }
    else{
      // Add new character to list
      bufferData[dataIndex++] = receiveData;
    }
    //Allow receive data again
    HAL_UART_Receive_IT(&huart1, &receiveData,1);
  }
}
/* End user functions*/


/***************************/
/***SYSTEM FUNCTIONS********/
/***************************/
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Configure the Systick interrupt time 
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);

    /**Configure the Systick 
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* TIM2 init function */
static void MX_TIM2_Init(void)
{

  TIM_ClockConfigTypeDef sClockSourceConfig;
  TIM_MasterConfigTypeDef sMasterConfig;
  TIM_OC_InitTypeDef sConfigOC;

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 24;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 255;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  HAL_TIM_MspPostInit(&htim2);

}

/* USART1 init function */
static void MX_USART1_UART_Init(void)
{

  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/** Configure pins as 
        * Analog 
        * Input 
        * Output
        * EVENT_OUT
        * EXTI
*/
static void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct;

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA1 PA2 PA4 PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitTypeDef GPIO_InitStruct_B;
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
  GPIO_InitStruct_B.Pin = GPIO_PIN_12;
  GPIO_InitStruct_B.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct_B.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct_B);
}

/***END SYSTEM FUCTIONS*****/

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  file: The file name as string.
  * @param  line: The line in file as a number.
  * @retval None
  */
void _Error_Handler(char *file, int line)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while(1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
  {
    if(GPIO_Pin==GPIO_PIN_0)
      {
        read = 1;
        dmpDataReady();
      }
  }
/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
