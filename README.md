# M314 Motion Tracker 
![Motion Tracker](./docs/Motion_Tracker.webp)  
![Motion Tracker](./docs/ss.png)  

This project uses [HLK-LD2450](https://www.hlktech.net/index.php?id=1182) and [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3) to build the M314 Motion Tracker display, sound effects and sensing — that USCM equipment featured in the movie *Aliens (1986)*.  

You can experience the vibe of the film — "anytime, anywhere"!

---

![HLK-LD2450](./docs/ld2450.jpg)
![CoreS3](./docs/s3.png)

| LD2450 | CoreS3 PORT.C |
| --- | --- |
| 5V | V（5V） |
| RX | T（TX / GPIO17） |
| TX | R（RX / GPIO18） |
| GND | G（GND） |

> [!TIP]  
> According to the HLK-LD2450 specs, it can sometimes detect motion even on the backside of the sensor.  
> If you want really high detection accuracy, you'll need some hardware tweaks to suppress reflections.
