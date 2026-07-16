/*************************************************
*
*  棉花纤维显微图像半径测量系统
*
*  单文件C语言版本
*
*  Part 1:
*  BMP读取
*  灰度化
*  BMP保存
*
*************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <math.h>



#define WIDTH 1280
#define HEIGHT 1024



/***********************
    图像数据
************************/


unsigned char gray[HEIGHT][WIDTH];


unsigned char binary[HEIGHT][WIDTH];


unsigned char skeleton[HEIGHT][WIDTH];


float distanceMap[HEIGHT][WIDTH];



/***********************
    BMP结构
************************/


#pragma pack(1)


typedef struct
{

    unsigned short type;


    unsigned int size;


    unsigned short reserved1;


    unsigned short reserved2;


    unsigned int offset;


}BMP_FILE_HEADER;





typedef struct
{

    unsigned int size;


    int width;


    int height;


    unsigned short planes;


    unsigned short bitCount;


    unsigned int compression;


    unsigned int imageSize;


    int xppm;


    int yppm;


    unsigned int clrUsed;


    unsigned int clrImportant;


}BMP_INFO_HEADER;




#pragma pack()



/************************************************
*
* 读取BMP
*
* 支持:
* 24bit BMP
*
************************************************/


int readBMP(char *filename)
{


    FILE *fp;


    fp=fopen(filename,"rb");



    if(fp==NULL)
    {

        printf("BMP打开失败!\n");

        return -1;

    }




    BMP_FILE_HEADER fileHeader;


    BMP_INFO_HEADER infoHeader;




    fread(&fileHeader,
          sizeof(BMP_FILE_HEADER),
          1,
          fp);



    fread(&infoHeader,
          sizeof(BMP_INFO_HEADER),
          1,
          fp);




    if(infoHeader.bitCount!=24)
    {


        printf("错误:只支持24位BMP\n");


        fclose(fp);


        return -1;

    }




    printf("图像尺寸:%d × %d\n",
           infoHeader.width,
           infoHeader.height);




    fseek(fp,
          fileHeader.offset,
          SEEK_SET);




    int width=infoHeader.width;


    int height=infoHeader.height;




    for(int y=height-1;y>=0;y--)
    {


        for(int x=0;x<width;x++)
        {


            unsigned char B;


            unsigned char G;


            unsigned char R;




            fread(&B,1,1,fp);


            fread(&G,1,1,fp);


            fread(&R,1,1,fp);




            /*
             RGB转灰度

             Y=
             0.299R+
             0.587G+
             0.114B

            */


            gray[y][x]
            =
            (unsigned char)
            (
             0.299*R+
             0.587*G+
             0.114*B
            );


        }

    }




    fclose(fp);



    printf("BMP读取成功\n");



    return 0;


}






/************************************************
*
* 保存BMP
*
* 输入:
* 0~255灰度数据
*
************************************************/


void saveBMP(char *filename,
             unsigned char image[HEIGHT][WIDTH])
{


    FILE *fp;


    fp=fopen(filename,"wb");



    if(fp==NULL)
    {

        printf("保存失败\n");

        return;

    }




    BMP_FILE_HEADER fileHeader;


    BMP_INFO_HEADER infoHeader;




    int fileSize=
    54+
    WIDTH*HEIGHT*3;





    fileHeader.type=0x4D42;


    fileHeader.size=fileSize;


    fileHeader.reserved1=0;


    fileHeader.reserved2=0;


    fileHeader.offset=54;






    infoHeader.size=40;


    infoHeader.width=WIDTH;


    infoHeader.height=HEIGHT;


    infoHeader.planes=1;


    infoHeader.bitCount=24;


    infoHeader.compression=0;


    infoHeader.imageSize=0;


    infoHeader.xppm=0;


    infoHeader.yppm=0;


    infoHeader.clrUsed=0;


    infoHeader.clrImportant=0;





    fwrite(&fileHeader,
           sizeof(fileHeader),
           1,
           fp);



    fwrite(&infoHeader,
           sizeof(infoHeader),
           1,
           fp);







    for(int y=HEIGHT-1;y>=0;y--)
    {


        for(int x=0;x<WIDTH;x++)
        {



            unsigned char c=image[y][x];



            /*
             BMP顺序:

             B
             G
             R

            */


            fwrite(&c,1,1,fp);


            fwrite(&c,1,1,fp);


            fwrite(&c,1,1,fp);



        }


    }




    fclose(fp);



    printf("生成:%s\n",filename);



}



/************************************************
*
* 保存二值图
*
************************************************/


void saveBinaryBMP(char *filename)
{

    unsigned char temp[HEIGHT][WIDTH];



    for(int y=0;y<HEIGHT;y++)
    {

        for(int x=0;x<WIDTH;x++)
        {


            if(binary[y][x])

                temp[y][x]=255;

            else

                temp[y][x]=0;


        }

    }



    saveBMP(filename,temp);

}




/************************************************
*
* 保存骨架图
*
************************************************/


void saveSkeletonBMP(char *filename)
{


    unsigned char temp[HEIGHT][WIDTH];


    for(int y=0;y<HEIGHT;y++)
    {

        for(int x=0;x<WIDTH;x++)
        {


            if(skeleton[y][x])

                temp[y][x]=255;

            else

                temp[y][x]=0;



        }


    }



    saveBMP(filename,temp);


}




/************************************************
*
* 保存距离变换图
*
************************************************/


void saveDistanceBMP(char *filename)
{


    unsigned char temp[HEIGHT][WIDTH];


    float max=0;



    for(int y=0;y<HEIGHT;y++)

        for(int x=0;x<WIDTH;x++)

            if(distanceMap[y][x]>max)

                max=distanceMap[y][x];





    for(int y=0;y<HEIGHT;y++)
    {


        for(int x=0;x<WIDTH;x++)
        {


            temp[y][x]
            =
            (unsigned char)
            (
             distanceMap[y][x]
             /
             max
             *
             255
            );


        }


    }



    saveBMP(filename,temp);


}
/*************************************************
*
* Part 2
*
* OTSU二值化
* 去噪
* 补洞
* Hilditch细化
*
*************************************************/



/************************************************
*
* OTSU最大类间方差法
*
************************************************/


int otsuThreshold()
{

    int histogram[256]={0};



    //统计灰度直方图

    for(int y=0;y<HEIGHT;y++)
    {

        for(int x=0;x<WIDTH;x++)
        {

            histogram[gray[y][x]]++;

        }

    }




    int total=WIDTH*HEIGHT;



    float sum=0;



    for(int i=0;i<256;i++)
    {

        sum+=i*histogram[i];

    }




    float sumB=0;


    int wB=0;


    int threshold=0;



    float maxVariance=0;




    for(int i=0;i<256;i++)
    {


        wB+=histogram[i];



        if(wB==0)

            continue;




        int wF=total-wB;



        if(wF==0)

            break;




        sumB+=
        (float)i*histogram[i];




        float mB=
        sumB/wB;



        float mF=
        (sum-sumB)/wF;





        float variance=
        (float)wB*wF*
        (mB-mF)*(mB-mF);





        if(variance>maxVariance)
        {

            maxVariance=variance;

            threshold=i;

        }



    }



    printf("OTSU阈值=%d\n",
           threshold);



    return threshold;


}







/************************************************
*
* 二值化
*
* 目标:
*
* 纤维=1
* 背景=0
*
************************************************/


void binaryProcess(int threshold)
{


    for(int y=0;y<HEIGHT;y++)
    {

        for(int x=0;x<WIDTH;x++)
        {



            if(gray[y][x]<threshold)

            {

                binary[y][x]=1;

            }

            else

            {

                binary[y][x]=0;

            }



        }

    }



}








/************************************************
*
* 3×3邻域去噪
*
************************************************/


void denoise()
{


    unsigned char temp[HEIGHT][WIDTH]={0};




    for(int y=1;y<HEIGHT-1;y++)
    {


        for(int x=1;x<WIDTH-1;x++)
        {



            int count=0;



            for(int j=-1;j<=1;j++)
            {

                for(int i=-1;i<=1;i++)
                {


                    count+=binary[y+j][x+i];


                }

            }





            /*
              周围像素超过4

              保留

            */


            if(count>=4)

                temp[y][x]=1;



        }


    }





    for(int y=0;y<HEIGHT;y++)
    {

        for(int x=0;x<WIDTH;x++)
        {

            binary[y][x]=temp[y][x];

        }

    }




}








/************************************************
*
* 形态学补洞
*
************************************************/


void fillHole()
{


    for(int y=1;y<HEIGHT-1;y++)
    {


        for(int x=1;x<WIDTH-1;x++)
        {


            if(binary[y][x]==0)
            {


                int count=0;



                for(int j=-1;j<=1;j++)
                {

                    for(int i=-1;i<=1;i++)
                    {

                        count+=binary[y+j][x+i];


                    }

                }




                /*
                 周围8个点
                 大部分为纤维
                 则填充

                */


                if(count>=7)

                    binary[y][x]=1;



            }


        }


    }



}







/************************************************
*
* Hilditch细化算法
*
************************************************/



//计算8邻域数量

int neighborCount(int x,int y)
{


    int n=0;



    for(int j=-1;j<=1;j++)
    {


        for(int i=-1;i<=1;i++)
        {


            if(i==0&&j==0)

                continue;



            n+=binary[y+j][x+i];


        }

    }


    return n;


}








//计算0->1变化次数

int transitionCount(int x,int y)
{


    int p[8];



    p[0]=binary[y-1][x];

    p[1]=binary[y-1][x+1];

    p[2]=binary[y][x+1];

    p[3]=binary[y+1][x+1];

    p[4]=binary[y+1][x];

    p[5]=binary[y+1][x-1];

    p[6]=binary[y][x-1];

    p[7]=binary[y-1][x-1];




    int A=0;



    for(int i=0;i<8;i++)
    {

        if(p[i]==0 &&
           p[(i+1)%8]==1)

            A++;


    }




    return A;


}







void hilditch()
{


    int change=1;




    while(change)
    {


        change=0;



        unsigned char remove[HEIGHT][WIDTH]={0};




        for(int y=1;y<HEIGHT-1;y++)
        {


            for(int x=1;x<WIDTH-1;x++)
            {



                if(binary[y][x]==1)
                {



                    int B=
                    neighborCount(x,y);



                    int A=
                    transitionCount(x,y);





                    /*
                     Hilditch条件

                    */


                    if(B>=2 &&
                       B<=6 &&
                       A==1)

                    {

                        remove[y][x]=1;


                    }




                }



            }


        }






        for(int y=1;y<HEIGHT-1;y++)
        {


            for(int x=1;x<WIDTH-1;x++)
            {


                if(remove[y][x])

                {

                    binary[y][x]=0;

                    change=1;


                }



            }


        }




    }





    //保存骨架

    for(int y=0;y<HEIGHT;y++)
    {

        for(int x=0;x<WIDTH;x++)
        {

            skeleton[y][x]=binary[y][x];

        }

    }




    printf("Hilditch细化完成\n");


}
/*************************************************
*
* Part 3
*
* 距离变换
* 半径计算
* 统计分析
* main函数
*
*************************************************/



/************************************************
*
* 距离变换
*
* Distance Transform
*
* 对二值图中每个纤维像素
* 计算距离背景的最短距离
*
************************************************/


void distanceTransform()
{


    //初始化

    for(int y=0;y<HEIGHT;y++)
    {

        for(int x=0;x<WIDTH;x++)
        {


            if(binary[y][x]==1)

                distanceMap[y][x]=9999;


            else

                distanceMap[y][x]=0;



        }

    }





    /*
    
    正向扫描

    左上 -> 右下

    */


    for(int y=1;y<HEIGHT;y++)
    {


        for(int x=1;x<WIDTH;x++)
        {



            if(distanceMap[y][x]>0)
            {



                float a=
                distanceMap[y-1][x]+1;



                float b=
                distanceMap[y][x-1]+1;





                if(a<distanceMap[y][x])

                    distanceMap[y][x]=a;



                if(b<distanceMap[y][x])

                    distanceMap[y][x]=b;



            }



        }


    }






    /*
    
    反向扫描

    右下 -> 左上

    */


    for(int y=HEIGHT-2;y>=0;y--)
    {


        for(int x=WIDTH-2;x>=0;x--)
        {


            if(distanceMap[y][x]>0)
            {


                float a=
                distanceMap[y+1][x]+1;



                float b=
                distanceMap[y][x+1]+1;




                if(a<distanceMap[y][x])

                    distanceMap[y][x]=a;



                if(b<distanceMap[y][x])

                    distanceMap[y][x]=b;



            }



        }


    }



    printf("距离变换完成\n");


}








/************************************************
*
* 半径统计
*
*
* 距离变换理论:
*
* r=D(x,y)
*
* 其中：
*
* (x,y) 为骨架点
*
* D为距离值
*
************************************************/


void radiusStatistic()
{


    FILE *fp1;


    FILE *fp2;




    fp1=fopen("output\\radius.txt",
              "w");



    fp2=fopen("output\\radius_distribution.csv",
              "w");





    if(fp1==NULL ||
       fp2==NULL)
    {


        printf("统计文件创建失败\n");

        return;


    }






    fprintf(fp2,
            "Radius(pixel),Frequency\n");






    int histogram[200]={0};





    float sum=0;


    float sum2=0;



    float maxRadius=0;


    float minRadius=9999;



    int number=0;






    for(int y=0;y<HEIGHT;y++)
    {



        for(int x=0;x<WIDTH;x++)
        {



            /*
            
            只统计骨架点
            
            */


            if(skeleton[y][x]==1)
            {



                float r=
                distanceMap[y][x];




                fprintf(fp1,
                        "%f\n",
                        r);





                int index=
                (int)(r+0.5);





                if(index<200)

                    histogram[index]++;







                if(r>maxRadius)

                    maxRadius=r;




                if(r<minRadius)

                    minRadius=r;





                sum+=r;



                sum2+=r*r;




                number++;





            }




        }



    }






    fclose(fp1);







    /*
    
    输出频率表
    
    */


    for(int i=0;i<200;i++)
    {


        if(histogram[i]>0)
        {


            fprintf(fp2,
                    "%d,%d\n",
                    i,
                    histogram[i]);


        }



    }




    fclose(fp2);







    if(number>0)
    {


        float average=
        sum/number;



        float variance=
        sum2/number-
        average*average;



        float std=
        sqrt(variance);





        printf("\n");
        printf("====================\n");

        printf("半径统计结果\n");

        printf("====================\n");


        printf("骨架点数量:%d\n",
               number);



        printf("平均半径:%f pixel\n",
               average);



        printf("最大半径:%f pixel\n",
               maxRadius);



        printf("最小半径:%f pixel\n",
               minRadius);



        printf("标准差:%f\n",
               std);



        printf("====================\n");



    }



}








/************************************************
*
* 主函数
*
************************************************/


int main()
{


    printf("=============================\n");

    printf(" 棉花纤维显微图像半径测量系统\n");

    printf("=============================\n");






    /*
    
    读取原始图片
    
    修改这里即可换图片

    */


    readBMP(

    "C:\\Users\\ASUS\\Desktop\\作业任务\\项目1 棉花纤维成熟度检测\\棉花纤维显微图像.bmp"

    );







    /*
    
    保存灰度图

    */


    saveBMP(
    "output\\1_gray.bmp",
    gray
    );







    /*
    
    OTSU
    
    */


    int T=
    otsuThreshold();





    /*
    
    二值化

    */


    binaryProcess(T);




    saveBinaryBMP(
    "output\\2_binary.bmp"
    );
    /*
    
    去噪

    */
    denoise();
    saveBinaryBMP(
    "output\\3_denoise.bmp"
    );
    /*
    
    补洞

    */
    fillHole();
    saveBinaryBMP(
    "output\\4_fill.bmp"
    );
    /*
    
    Hilditch细化

    */
    hilditch();
    saveSkeletonBMP(
    "output\\5_skeleton.bmp"
    );
    /*
    
    距离变换

    */

    distanceTransform();
    saveDistanceBMP(
    "output\\6_distance.bmp"
    );
    /*
    
    半径统计

    */
    radiusStatistic();
    printf("\n全部处理完成!\n");
    system("pause");
    
    return 0;
}
