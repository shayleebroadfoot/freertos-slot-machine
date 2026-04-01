/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * SARAH WAS HERE
  *
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "usb_host.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// State Definitions
#define STATE_BOOT         0
#define STATE_IDLE         1
#define STATE_ADD_CREDITS  2
#define STATE_PLAYING      3
#define STATE_CASH_OUT     4

// Game Constants
#define MAX_WAGER          10
#define BOOT_TIME_MS       3000
#define CASHOUT_TIME_MS    5000

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

I2S_HandleTypeDef hi2s3;

RNG_HandleTypeDef hrng;

SPI_HandleTypeDef hspi1;

/* Definitions for gameTask */
osThreadId_t gameTaskHandle;
const osThreadAttr_t gameTask_attributes = {
  .name = "gameTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for inputTask */
osThreadId_t inputTaskHandle;
const osThreadAttr_t inputTask_attributes = {
  .name = "inputTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for displayTask */
osThreadId_t displayTaskHandle;
const osThreadAttr_t displayTask_attributes = {
  .name = "displayTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for creditTask */
osThreadId_t creditTaskHandle;
const osThreadAttr_t creditTask_attributes = {
  .name = "creditTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */

// Volatile because these change across different tasks
volatile int STATE = STATE_BOOT;

uint32_t currentBalance = 0;
uint32_t currentWager = 1;
uint32_t totalWinnings = 0;

// Variables for the RNG logic
uint32_t targetValue = 0; // The number to match (1-9)
uint32_t userValue = 0;   // The randomly generated result of the spin

volatile uint32_t last_press_tick = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2S3_Init(void);
static void MX_SPI1_Init(void);
static void MX_RNG_Init(void);
void StartGameTask(void *argument);
void StartInputTask(void *argument);
void startDisplayTask(void *argument);
void startCreditTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// 8-bit I2C address for PCF8574T (0x27 << 1)
#define LCD_ADDR 0x4E

/**
 * @brief Sends a command to the LCD (RS = 0)
 */
void lcd_send_cmd(char cmd)
{
    char data_u, data_l;
    uint8_t data_t[4];
    data_u = (cmd & 0xf0);
    data_l = ((cmd << 4) & 0xf0);
    // Backlight is bit 3 (0x08), Enable is bit 2 (0x04), RS is bit 0 (0x01)
    data_t[0] = data_u | 0x0C;  // Backlight ON, Enable HIGH, RS LOW
    data_t[1] = data_u | 0x08;  // Backlight ON, Enable LOW, RS LOW
    data_t[2] = data_l | 0x0C;  // Lower nibble Enable HIGH
    data_t[3] = data_l | 0x08;  // Lower nibble Enable LOW
    HAL_I2C_Master_Transmit(&hi2c1, LCD_ADDR, (uint8_t *)data_t, 4, 100);
}

/**
 * @brief Sends data/characters to the LCD (RS = 1)
 */
void lcd_send_data(char data)
{
    char data_u, data_l;
    uint8_t data_t[4];
    data_u = (data & 0xf0);
    data_l = ((data << 4) & 0xf0);
    data_t[0] = data_u | 0x0D;  // Backlight ON, Enable HIGH, RS HIGH
    data_t[1] = data_u | 0x09;  // Backlight ON, Enable LOW, RS HIGH
    data_t[2] = data_l | 0x0D;  // Lower nibble Enable HIGH
    data_t[3] = data_l | 0x09;  // Lower nibble Enable LOW
    HAL_I2C_Master_Transmit(&hi2c1, LCD_ADDR, (uint8_t *)data_t, 4, 100);
}

/**
 * @brief Positions the cursor: row (0-1), col (0-15)
 */
void lcd_put_cur(int row, int col)
{
    switch (row)
    {
        case 0: col |= 0x80; break;
        case 1: col |= 0xC0; break;
    }
    lcd_send_cmd(col);
}

/**
 * @brief Initializes the LCD into 4-bit mode
 */
void lcd_init(void)
{
    // 1. Wait for stable power
    osDelay(100);

    // 2. Force 8-bit mode 3 times to sync (Standard HD44780 procedure)
    lcd_send_cmd(0x33);
    osDelay(5);
    lcd_send_cmd(0x32);
    osDelay(1);

    // 3. Now set 4-bit mode, 2 lines, 5x8 font
    lcd_send_cmd(0x28);
    osDelay(1);

    // 4. Display Control (Display ON, Cursor OFF)
    lcd_send_cmd(0x0C);
    osDelay(1);

    // 5. Clear Display
    lcd_send_cmd(0x01);
    osDelay(2);

    // 6. Entry Mode Set
    lcd_send_cmd(0x06);
    osDelay(1);
}

/**
 * @brief Sends a full string of text to the LCD
 */
void lcd_send_string(char *str)
{
    while (*str) lcd_send_data(*str++);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_I2S3_Init();
  MX_SPI1_Init();
  MX_RNG_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  // Insert semaphores here or some shite
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of gameTask */
  gameTaskHandle = osThreadNew(StartGameTask, NULL, &gameTask_attributes);

  /* creation of inputTask */
  inputTaskHandle = osThreadNew(StartInputTask, NULL, &inputTask_attributes);

  /* creation of displayTask */
  displayTaskHandle = osThreadNew(startDisplayTask, NULL, &displayTask_attributes);

  /* creation of creditTask */
  creditTaskHandle = osThreadNew(startCreditTask, NULL, &creditTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2S3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S3_Init(void)
{

  /* USER CODE BEGIN I2S3_Init 0 */

  /* USER CODE END I2S3_Init 0 */

  /* USER CODE BEGIN I2S3_Init 1 */

  /* USER CODE END I2S3_Init 1 */
  hi2s3.Instance = SPI3;
  hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_96K;
  hi2s3.Init.CPOL = I2S_CPOL_LOW;
  hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S3_Init 2 */

  /* USER CODE END I2S3_Init 2 */

}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port, OTG_FS_PowerSwitchOn_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : BTN_UP_Pin BTN_DOWN_Pin BTN_ADD_CREDIT_Pin BTN_EXIT_Pin */
  GPIO_InitStruct.Pin = BTN_UP_Pin|BTN_DOWN_Pin|BTN_ADD_CREDIT_Pin|BTN_EXIT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : CS_I2C_SPI_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS_I2C_SPI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = OTG_FS_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PDM_OUT_Pin */
  GPIO_InitStruct.Pin = PDM_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(PDM_OUT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_LEVER_SPIN_Pin */
  GPIO_InitStruct.Pin = BTN_LEVER_SPIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(BTN_LEVER_SPIN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BOOT1_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BOOT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CLK_IN_Pin */
  GPIO_InitStruct.Pin = CLK_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(CLK_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD4_Pin LD3_Pin LD5_Pin LD6_Pin
                           Audio_RST_Pin */
  GPIO_InitStruct.Pin = LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_OverCurrent_Pin */
  GPIO_InitStruct.Pin = OTG_FS_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_FS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MEMS_INT2_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// IDLE screen (Nothing displayed for now)
void displayIdleScreen(void)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12 | GPIO_PIN_15, GPIO_PIN_RESET); // Others OFF
}

// CREDITS screen turns on GREEN LED
void displayCreditsScreen(void)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET); // GREEN ON
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_RESET); // Others OFF
}

// PLAYING screen turns on BLUE LED
void displayPlayingScreen(void)
{
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_SET); // BLUE ON
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET); // Others OFF
}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == BTN_LEVER_SPIN_Pin)
  {
    // DEBOUNCE: If the last press was less than 250ms ago, ignore this one
    if ((HAL_GetTick() - last_press_tick) < 250)
    {
      return;
    }
    last_press_tick = HAL_GetTick(); // Record the time of this valid press

    // Now execute your state logic
    if (STATE == STATE_IDLE)
    {
      STATE = STATE_ADD_CREDITS;
    }
    else if (STATE == STATE_ADD_CREDITS)
    {
      if (currentBalance > 0)
      {
        STATE = STATE_PLAYING;
      }
    }
    else if (STATE == STATE_PLAYING)
    {
      // --- THE SPIN ---
      uint32_t raw_rng;
      HAL_RNG_GenerateRandomNumber(&hrng, &raw_rng);
      targetValue = (raw_rng % 9) + 1;

      HAL_RNG_GenerateRandomNumber(&hrng, &raw_rng);
      userValue = (raw_rng % 9) + 1;

      if (userValue == targetValue)
      {
        currentBalance += currentWager;
      }
      else
      {
        currentBalance -= currentWager;
        if (currentBalance == 0) STATE = STATE_ADD_CREDITS;
      }
    }
  }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartGameTask */
/**
  * @brief  Function implementing the gameTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartGameTask */
void StartGameTask(void *argument)
{
  /* init code for USB_HOST */
  MX_USB_HOST_Init();
  /* USER CODE BEGIN 5 */

  // --- BOOT LOCKOUT (STATE 0) ---
  // We stay here for 3 seconds. Buttons will be ignored in the InputTask later.
  HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_SET); // ORANGE LED indicates BOOTING

  osDelay(BOOT_TIME_MS); // Simple delay during the startup phase

  STATE = STATE_IDLE; // Move to IDLE after 3 seconds
  HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_RESET); // BOOT LED (ORANGE) OFF

  /* Infinite loop */
  for(;;)
  {
    // The main game logic will go here in the next step
    osDelay(10);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartInputTask */
/**
* @brief Function implementing the inputTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartInputTask */
void StartInputTask(void *argument)
{
  /* USER CODE BEGIN StartInputTask */
  for(;;)
  {

  // --- ADD CREDIT BUTTON (PE5) ---
	if (HAL_GPIO_ReadPin(GPIOE, BTN_ADD_CREDIT_Pin) == GPIO_PIN_RESET)
	{
	  // If we are playing, go back to the credit screen
	  if (STATE == STATE_PLAYING)
	  {
		  STATE = STATE_ADD_CREDITS;
	  }
	  // If we were already in IDLE, move to ADD_CREDITS
	  else if (STATE == STATE_IDLE)
	  {
		  STATE = STATE_ADD_CREDITS;
	  }
	  osDelay(200); // Debounce
	}
    // Only allow input if we are in a state that accepts it
    if (STATE == STATE_ADD_CREDITS || STATE == STATE_PLAYING)
    {
      // --- UP BUTTON (PE2) ---
      if (HAL_GPIO_ReadPin(GPIOE, BTN_UP_Pin) == GPIO_PIN_RESET)
      {
        if (STATE == STATE_ADD_CREDITS)
        {
           currentBalance++; // Add money
        }
        else if (STATE == STATE_PLAYING)
        {
           if (currentWager < MAX_WAGER && currentWager < currentBalance)
           {
              currentWager++; // Increase bet
           }
        }
        osDelay(200); // Debounce: wait for finger to lift
      }

      // --- DOWN BUTTON (PE4) ---
      if (HAL_GPIO_ReadPin(GPIOE, BTN_DOWN_Pin) == GPIO_PIN_RESET)
      {
        if (STATE == STATE_ADD_CREDITS)
        {
           if (currentBalance > 0) currentBalance--; // Prevent negative balance
        }
        else if (STATE == STATE_PLAYING)
        {
           if (currentWager > 1) currentWager--; // Minimum bet is 1
        }
        osDelay(200); // Debounce
      }

      // --- EXIT / CASH OUT BUTTON (PE6) ---
      if (HAL_GPIO_ReadPin(GPIOE, BTN_EXIT_Pin) == GPIO_PIN_RESET)
      {
        if (STATE == STATE_ADD_CREDITS || STATE == STATE_PLAYING)
        {
            // 1. Record the winnings for the displayTask to show
            totalWinnings = currentBalance;

            // 2. Clear values
            currentBalance = 0;
            currentWager = 1;

            // 3. THE LOCKOUT: 5 seconds of Red LED
            HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET);
            osDelay(5000);
            HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_RESET);

            // 4. Reset to IDLE ONLY after the lockout is over
            STATE = STATE_IDLE;
        }
        osDelay(200); // Debounce
      }
    }
    osDelay(20); // Let the CPU breathe
  }
  /* USER CODE END StartInputTask */
}

/* USER CODE BEGIN Header_startDisplayTask */
/**
* @brief Function implementing the displayTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_startDisplayTask */
void startDisplayTask(void *argument)
{
  /* USER CODE BEGIN startDisplayTask */
  // 1. Initialize the LCD hardware
  lcd_init();

  char buffer[17]; // Buffer to hold one line of text (16 chars + null terminator)
  int lastState = -1;

  for(;;)
  {
    // Only clear the screen when we CHANGE states to prevent flickering
    if (STATE != lastState)
    {
        lcd_send_cmd(0x01); // Clear Display
        lastState = STATE;
        // Turn off Green, Red, Blue (Orange is handled by Boot task)
        HAL_GPIO_WritePin(GPIOD, LD4_Pin | LD5_Pin | LD6_Pin, GPIO_PIN_RESET);
    }

    switch(STATE)
    {
      case STATE_BOOT: // State 0
        lcd_put_cur(0, 0);
        lcd_send_string("System");
        lcd_put_cur(1, 0);
        lcd_send_string("Initializing...");
        break;

      case STATE_IDLE: // State 1
        lcd_put_cur(0, 0);
        lcd_send_string("Welcome! Press");
        lcd_put_cur(1, 0);
        lcd_send_string("OK to Start");
        break;

      case STATE_ADD_CREDITS: // State 2
        lcd_put_cur(0, 0);
        lcd_send_string("Add Credits:");
        // Use sprintf to put the currentBalance into our buffer
        sprintf(buffer, "Balance: %lu", currentBalance);
        lcd_put_cur(1, 0);
        lcd_send_string(buffer);
        HAL_GPIO_WritePin(GPIOD, LD4_Pin, GPIO_PIN_SET); // Green LED ON
        break;

      case STATE_PLAYING:
		lcd_put_cur(0, 0);
	    sprintf(buffer, "B:%lu W:%lu", currentBalance, currentWager);
		lcd_send_string(buffer);

		// Show the result of the last spin
		lcd_put_cur(1, 0);
		sprintf(buffer, "Tgt:%lu Res:%lu", targetValue, userValue);
		lcd_send_string(buffer);
		break;

	  case STATE_CASH_OUT:
		lcd_put_cur(0, 0);
		lcd_send_string("Cashing Out...");
		sprintf(buffer, "Win:%lu Resetting", totalWinnings);
		lcd_put_cur(1, 0);
		lcd_send_string(buffer);
		HAL_GPIO_WritePin(GPIOD, LD5_Pin, GPIO_PIN_SET); // Red LED ON
		break;
    }

    osDelay(100); // Refresh every 100ms
  }
  /* USER CODE END startDisplayTask */
}

/* USER CODE BEGIN Header_startCreditTask */
/**
* @brief Function implementing the creditTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_startCreditTask */
void startCreditTask(void *argument)
{
  /* USER CODE BEGIN startCreditTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END startCreditTask */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
