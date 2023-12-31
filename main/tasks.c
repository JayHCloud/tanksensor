#include <include/tasks.h>
//#define tempAlarm          (1 << 0)    Bit 0 for temperature alarm
//#define toggleLight        (1 << 1)    Bit 1 for toggle light signal
//#define takeTemperature    (1 << 2)    Bit 1 for toggle light signal

EventGroupHandle_t tasksGroup;

TaskHandle_t tempTaskHandle = NULL;
TaskHandle_t emergencyTaskHandle = NULL;
TaskHandle_t apiTaskHandle = NULL;

static float currentTemp = 0;  //hide in task/func so not shared
static float previousTemp = 0;   //hide in task/func
static float tempGap = 0; //hide this in func so not shared and make it return t/f if out of bounds
static float averageTemp = 0;   //hide in task/func

#define TASK_STACK_SIZE 16000 // Arbitrarily bigger than the stack high watermark of 7k i measured. 
#define BUFFER_SIZE 20



void tasks_init(){ 

    tasksGroup = xEventGroupCreate();
    xTaskCreatePinnedToCore(temp_task, "temp_task", TASK_STACK_SIZE, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(api_task, "api_task", TASK_STACK_SIZE, NULL, 1, NULL, 1);


}



void temp_task(void *arg) {      //Will only be red for 30sec before the next reading normalizes it. So only good for quick +-2 water tests unless i keep a larger/wider pool of #'s to get an average

    static int count = 0;
    while (1) {
        EventBits_t bits = xEventGroupGetBits(tasksGroup);

        previousTemp = currentTemp;
        currentTemp = *(ds18b20_get_temp_ptr());
        get_average_temp(currentTemp);
        get_temp_gap();

        if (count >= 10 ) {
            ds18b20_send_mqtt();
            count = 0;
        }

        if ((tempGap >= 2) && !(bits && tempAlarm)) { //Sets alarm bit and turns led red
            xEventGroupSetBits(tasksGroup, tempAlarm);
            led_red();
        }

        if (tempGap < 2 && (bits & tempAlarm))  {  //Unsets alarm bit and turns led off
            led_off();
            xEventGroupClearBits(tasksGroup, tempAlarm);
        }
        count++;
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}


void api_task(void *arg)
{

    while(1){
        EventBits_t bits = xEventGroupWaitBits(tasksGroup, tempAlarm | toggleLight | takeTemperature, false, false, portMAX_DELAY);

        if (((bits & tempAlarm) == 0) && ((bits & toggleLight) != 0)) {
            toggle_led();
            xEventGroupClearBits(tasksGroup, toggleLight);
        }

        if (((bits & takeTemperature) != 0)) {
            ds18b20_send_mqtt();
            xEventGroupClearBits(tasksGroup, takeTemperature);
        }



    }
}


void get_temp_gap(){

    if(( currentTemp !=0) && (averageTemp !=0)){ 
        tempGap = currentTemp - averageTemp;
        tempGap = (tempGap < 0) ? -tempGap : tempGap;
    }

    else{tempGap = 0;
    }
}


// Maintains the running average for emergency alarm
void get_average_temp(float value) {

    static float valueBuffer[BUFFER_SIZE] = {0}; 
    static float runningSum = 0;
    static bool is_init = false;
    static int index = 0;  

    if(is_init)
    {
    // Subtract the old value at the index from the running sum before overwriting
    runningSum -= valueBuffer[index];
    valueBuffer[index] = value; 
    runningSum += value; 
    averageTemp = runningSum / (BUFFER_SIZE);
    }


    if( !is_init && value > 1.0 && value < 100.00 ){//Somethingsomething magic numbers  //fills buffer with first good temp once sensor is stable then signals that its init
        for (int i = 0; i <= BUFFER_SIZE; i++) {
            valueBuffer[i] = value;  
        }
        runningSum = value * (BUFFER_SIZE);
        is_init = true;

    }

    
    if(index >= BUFFER_SIZE)  //resets index
    {
        index = 0;
    }

     
}


