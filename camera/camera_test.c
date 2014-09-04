#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h> /* getopt_long() */
#include <fcntl.h> /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <termios.h>
//#include <stropts.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h> /* for videodev2.h */
#include <linux/videodev2.h>

#define SCREEN_WIDTH    640
#define SCREEN_HEIGHT   480

typedef struct camera_cfg{
	int  fd;
	int x;
	int y;
	int w;
	int h;
	int pic_w;
	int pic_h;
	char mclk;
	int videobuf_cnt;
} camera_cfg_t;



enum{
	UC13X_EXTCMD_G_BUFADDR = 0x80001000,
	UC13X_EXTCMD_ALLOC_BUF,
	UC13X_EXTCMD_FREE_BUF,
	UC13X_EXTCMD_IMG_ZOOM,
	UC13X_EXTCMD_IMG_ROTATE,
};

enum{
	UC13X_CAM_ROTATOR_0 = 0x80001000,
	UC13X_CAM_ROTATOR_90,
	UC13X_CAM_ROTATOR_180,
	UC13X_CAM_ROTATOR_270,
	UC13X_CAM_ROTATOR_FLIP,
	UC13X_CAM_ROTATOR_MIEEOR,

};

struct uc13x_img_zoom{
	unsigned int src_index;
	unsigned int dst_index;
	unsigned int imageformat;
	unsigned int srcwidth;
	unsigned int srcheight;
	unsigned int dstwidth;
	unsigned int dstheight;

};


struct uc13x_img_rot{
	unsigned int src_id;
	unsigned int dst_id;
	unsigned int dst_offset;
	unsigned int rotate_mode;
	unsigned int width;
	unsigned int height;
};


struct uc13x_mem_info{
	unsigned int blk_id;
	unsigned int phy_addr;
	unsigned int vir_addr;
};

struct un13x_frame_info{
	unsigned int size;
	void * buf;
};

struct uc13x_extcmd{
	unsigned int cmd;
	union{
		unsigned int id;	/*UC13X_EXTCMD_G_BUFADDR*/
		unsigned int phy_addr;	/*output give addr for video buffer*/
		struct uc13x_mem_info mem_info;
		struct un13x_frame_info frame;
		struct uc13x_img_zoom zoom;
		struct uc13x_img_rot rot;
	}u;

};


FILE* fyuv = NULL;

int camera_extioctl(int fd, struct uc13x_extcmd* p_cmd){
	int ret = 0;
	unsigned int input;
	input = (unsigned int )p_cmd;
	ret = ioctl (fd, VIDIOC_S_INPUT, &input);
	if(ret !=0){
		printf ("VIDIOC_S_INPUT error %d\n",ret);
		return -1;
	}
	return 0;
}

int camera_zoom_img(int fd, int src,int dst ,camera_cfg_t *cfg){
	struct uc13x_extcmd cmd;
	cmd.cmd = UC13X_EXTCMD_IMG_ZOOM;
	cmd.u.zoom.src_index  = src;
	cmd.u.zoom.dst_index  = dst;
	cmd.u.zoom.imageformat = 0;
	cmd.u.zoom.srcwidth = cfg->w;
	cmd.u.zoom.srcheight = cfg->h;
	cmd.u.zoom.dstwidth = cfg->pic_w;
	cmd.u.zoom.dstheight = cfg->pic_h;

	return camera_extioctl(fd, &cmd);

}

#define VIDEO_OUT_DBG_EN

typedef enum{
	PF_RGB565,
	PF_RGB888,
	PF_YUV420,
	PF_YUV420I,
	PF_YUV422
}PIXEL_FORMAT;

#define ULONG unsigned long 

#ifdef VIDEO_OUT_DBG_EN 
#define VIDEO_OUT_DBG   printf
#else
#define VIDEO_OUT_DBG   0,
#endif

#define VIDEO_OUT_ERR   printf

typedef enum{
	tp_video,
	tp_osd
}video_type;

typedef struct{
	video_type type;               
	int idev;       //v4l2 device handle
	int buf_count;  //display buffer number
	int buf_size;   
	int index;      //current display buffer id 
	unsigned char **buff;     //display buffer buffer addr 
	int *offsett;   //return by driver

	int pixel_format;//input pic pixel format
	int src_x;      //input pic width
	int src_y;      //input pic height


	int left;// Specifies the x-coordinate of the upper-left corner of the input pic on screen.
	int top; //Specifies the y-coordinate of the upper-left corner of the input pic on screen.
	int win_x;// Specifies the width of the input pic on screen.
	int win_y;// Specifies the height of the input pic on screen.
	int bpp;    //bits per pixel
	int pic_size;
}video, *p_video;

typedef struct{
	PIXEL_FORMAT format;

	int width;     //src picture width 

	int height;    //src picture height 

	int disp_top;  //ldc pic display position

	int disp_left; //ldc pic display position

	int disp_width; //ldc pic display width

	int disp_height; //ldc pic display height

	p_video pvideo; // camera used video
}cv_param;


static int set_video_param(video *pvideo,cv_param *p_cv_param)
{
	struct v4l2_format format;
	struct v4l2_requestbuffers req_buffers;
	struct v4l2_buffer qery_buffer;
	int i, k;

	memset((void *) &format, 0, sizeof(struct v4l2_format));

	/*  set video param */
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	if (pvideo->type == tp_video){
		format.fmt.pix.width = p_cv_param->width;
		format.fmt.pix.height = p_cv_param->height;
	}else{
		format.fmt.pix.width = p_cv_param->width;
		format.fmt.pix.height = p_cv_param->height;
	}

	if (PF_RGB565 == p_cv_param->format){
		format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
		format.fmt.pix.field = V4L2_FIELD_ANY;
		format.fmt.pix.bytesperline = format.fmt.pix.width * 2;
		format.fmt.pix.sizeimage = format.fmt.pix.bytesperline * format.fmt.pix.height;
		format.fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	}else if (PF_YUV420 == p_cv_param->format){
		format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
		format.fmt.pix.field = V4L2_FIELD_ANY;
		format.fmt.pix.bytesperline = format.fmt.pix.width * 3 / 2;
		format.fmt.pix.sizeimage = format.fmt.pix.bytesperline * format.fmt.pix.height;
		format.fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	}else{
		VIDEO_OUT_ERR("[set_video_param]not supported pixel format\n");
		return -1;
	}

	i = ioctl(pvideo->idev, VIDIOC_S_FMT, &format);
	if (i == 0){
		pvideo->pixel_format = p_cv_param->format;
		pvideo->src_x = format.fmt.pix.width;
		pvideo->src_y = format.fmt.pix.height;
		pvideo->bpp = (PF_RGB565 == p_cv_param->format) ? 16 : 12 ;
		pvideo->pic_size = (PF_RGB565 == p_cv_param->format) ?
			(pvideo->src_x * pvideo->src_y * 2) :
			(pvideo->src_x * pvideo->src_y * 3 / 2);
	}else{
		VIDEO_OUT_ERR("[set_video_param]set video param err\n");
		return -1;
	}

	/*  set video output param */

	if (pvideo->type == tp_video){
		format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	}else{
		format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY;
	}

	i = ioctl(pvideo->idev, VIDIOC_G_FMT, &format);
	if (i != 0){
		VIDEO_OUT_ERR("[set_video_param]get video output param err\n");
		return -1;
	}

	format.fmt.win.w.left = p_cv_param->disp_left;
	format.fmt.win.w.top = p_cv_param->disp_top;

	if (pvideo->type == tp_video){
		format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
		format.fmt.win.w.width = p_cv_param->disp_width;
		format.fmt.win.w.height = p_cv_param->disp_height;
	}else{
		format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY;
		format.fmt.win.w.width = p_cv_param->disp_width;
		format.fmt.win.w.height = p_cv_param->disp_height;
	}

	i = ioctl(pvideo->idev, VIDIOC_S_FMT, &format);
	if (i != 0){
		VIDEO_OUT_ERR("[set_video_param]set video param  failed\n");
		return -1;
	}

	pvideo->win_x = format.fmt.win.w.width;
	pvideo->win_y = format.fmt.win.w.height;
	pvideo->left = p_cv_param->disp_left;
	pvideo->top = p_cv_param->disp_top;


	/* request buffers */
	memset(&req_buffers, 0, sizeof(req_buffers));

	req_buffers.count = pvideo->buf_count;

	req_buffers.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	req_buffers.memory = V4L2_MEMORY_MMAP;

	VIDEO_OUT_DBG("[set_video_param]req buffer :(%d-%d-%d)\n",
			req_buffers.count,
			req_buffers.type,
			req_buffers.memory);

	i = ioctl(pvideo->idev, VIDIOC_REQBUFS, &req_buffers);
	if (i != 0){
		VIDEO_OUT_ERR("[set_video_param]driver malloc buffers err\n");
		return -1;
	} 

	/* query buffer */
	memset(&qery_buffer, 0, sizeof(qery_buffer));

	for (k = 0;k < pvideo->buf_count ;k++){
		qery_buffer.index = k;

		qery_buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

		qery_buffer.memory = V4L2_MEMORY_MMAP;

		i = ioctl(pvideo->idev, VIDIOC_QUERYBUF, &qery_buffer);
		if (i != 0){
			VIDEO_OUT_ERR("[set_video_param]driver malloc buffers err\n");
			return -1;
		}

		pvideo->offsett[k] = qery_buffer.m.offset; 
		pvideo->buf_size = qery_buffer.length;



		VIDEO_OUT_ERR("[set_video_param] buf size = %x \toffset = %x\n",
				pvideo->buf_size,
				pvideo->offsett[k]);

		pvideo->buff[k] = mmap(NULL,
				pvideo->buf_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				pvideo->idev,
				pvideo->offsett[k]);

		if (pvideo->buff[k] == (unsigned char *) 0xffffffff){
			VIDEO_OUT_ERR("[set_video_param]map buffers err = %d \n", errno);
			return -1;
		}

		VIDEO_OUT_DBG("[set_video_param]qery buffer(%d:%x:%x)\n",
				k,
				pvideo->offsett[k],
				pvideo->buff[k]);
	}

	pvideo->index = 0;
	return i;
}

/*
 *init video handle pic buffer,
 * temp set 3 frame buffer.
 */
static int init_video(video_type type,p_video pvideo,int ibuf)
{
	int i;

	pvideo->type = type;
	pvideo->buf_count = ibuf;  // internal buffer num , for flip display

	pvideo->buff = (unsigned char * *) malloc(sizeof(char *) * ibuf);
	if (pvideo->buff == NULL){
		VIDEO_OUT_ERR("[init_video]malloc memory err\n");
		return -1;
	}

	pvideo->index = 0;  //init index

	for (i = 0;i < ibuf;i++){
		pvideo->buff[i] = NULL;
	}

	pvideo->offsett = (int *) malloc(sizeof(int) * ibuf);
	if (pvideo->offsett == NULL){
		//not free memory
		VIDEO_OUT_ERR("[init_video]malloc memory err\n");
		return -1;
	}

	if (type == tp_video){
		pvideo->idev = open("/dev/video0", O_RDWR);
	}else{
		pvideo->idev = open("/dev/video1", O_RDWR);
	}
	VIDEO_OUT_ERR("opend device pvideo->idev is %p\n",pvideo->idev);

	if (pvideo->idev == 0){
		free(pvideo->buff);
		pvideo->buff = NULL;
		VIDEO_OUT_ERR("[init_video]open video device err\n");
		return -1;
	}

	return 0;
}

static void uninit_video(p_video pvideo)
{
	int k = 0;
	VIDEO_OUT_ERR("unmap video mem"); 
	for (k = 0;k < pvideo->buf_count ;k++){
		munmap(pvideo->buff[k],pvideo->buf_size);
	}
	if (pvideo->buff){
		free(pvideo->buff);
	}

	if (pvideo->offsett){
		free(pvideo->offsett);
	}
	if (pvideo->idev){
		close(pvideo->idev);
		VIDEO_OUT_ERR("closed device pvideo->idev is %p\n",pvideo->idev);
		VIDEO_OUT_ERR("[uninit_video]close video device\n");
	}

	memset((void *) pvideo, 0, sizeof(video));
}

static void init_video_buffer(p_video pvideo)
{
	int i, j;
	unsigned short * pframe;
	struct rgb565{
		unsigned short r : 5;
		unsigned short g : 6;
		unsigned short b : 5;
	};

	union{
		struct rgb565 rgb565;
		unsigned short v;
	}rgb[3];

	if (PF_RGB565 == pvideo->pixel_format){
		rgb[0].rgb565.r = 0x0;
		rgb[0].rgb565.g = 0;
		rgb[0].rgb565.b = 0;

		rgb[1].rgb565.r = 0;
		rgb[1].rgb565.g = 0x0;
		rgb[1].rgb565.b = 0;

		rgb[2].rgb565.r = 0;
		rgb[2].rgb565.g = 0;
		rgb[2].rgb565.b = 0x0;


		for (i = 0;i < pvideo->buf_count;i++){
			pframe = (unsigned short *) pvideo->buff[i];

			VIDEO_OUT_ERR("[init_video_buffer ]PF_RGB565 pframe 0x%x pic_size %d\n",
					pframe,
					pvideo->pic_size);

			for (j = 0;j < pvideo->pic_size / 2;j++){
				pframe[j] = rgb[i % 4].v;
			}
		}
	}else if (PF_YUV420 == pvideo->pixel_format){
		int i, j;
		char pff[3];
		unsigned char * pframe;
		char f0 = 0x0;
		char f1 = 0x0;
		char f2 = 0x0;

		pff[0] = f0;
		pff[1] = f1;
		pff[2] = f2;

		for (i = 0;i < pvideo->buf_count;i++){
			pframe = (unsigned char *) pvideo->buff[i];
			VIDEO_OUT_ERR("[init_video_buffer ]PF_YUV420 pframe 0x%x pic_size %d\n",
					pframe,
					pvideo->pic_size);
			for (j = 0;j < pvideo->pic_size;j++){
				pframe[j] = pff[i % 3];
			}
		}
	}else{
		VIDEO_OUT_ERR("[init_video_buffer ]not supported pixel_format \n");
	}
}

static void enable_video(p_video pvideo)
{
	ioctl(pvideo->idev, VIDIOC_STREAMON, 0);
}

static void disable_video(p_video pvideo)
{
	ioctl(pvideo->idev, VIDIOC_STREAMOFF, 0);
}

static int set_position(p_video pvideo,int x,int y)
{
	int i;
	struct v4l2_format format;

	memset((void *) &format, 0, sizeof(struct v4l2_format));

	if (pvideo->type == tp_video){
		format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	}else//tp_osd
	{
		format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY;
	}

	i = ioctl(pvideo->idev, VIDIOC_G_FMT, &format);
	if (i == 0){
		format.fmt.win.w.left = x;
		format.fmt.win.w.top = y;
		ioctl(pvideo->idev, VIDIOC_S_FMT, &format);
	}

	return i;
}

static int qbuf_video(p_video pvideo)
{
	struct v4l2_buffer vbuffer;

	memset((void *) &vbuffer, 0, sizeof(struct v4l2_buffer));
	vbuffer.index = pvideo->index;
	vbuffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vbuffer.memory = V4L2_MEMORY_MMAP;
	vbuffer.m.offset = pvideo->offsett[pvideo->index];
	return ioctl(pvideo->idev, VIDIOC_QBUF, &vbuffer);
}

//now only  support yuv420 
ULONG create_videoout(cv_param *p_cv_param)
{
	p_video pvideo;
	int irtn;
	video_type type = tp_video; 

	if (!p_cv_param){
		VIDEO_OUT_ERR("[create_videoout], in param p_cv_param error!!!\n");
		return 0;
	}

	if (p_cv_param->disp_height > SCREEN_HEIGHT - p_cv_param->disp_top){
		VIDEO_OUT_ERR("[create_videoout], in param p_cv_param->disp_height not fit screen !!!\n");
		p_cv_param->disp_height = SCREEN_HEIGHT - p_cv_param->disp_top;
	}
	if (p_cv_param->disp_width > SCREEN_WIDTH - p_cv_param->disp_left){
		VIDEO_OUT_ERR("[create_videoout], in param p_cv_param->disp_width not fit screen !!!\n");
		p_cv_param->disp_width = SCREEN_WIDTH - p_cv_param->disp_left;
	}
	if ((!p_cv_param->width) || (!p_cv_param->width)){
		VIDEO_OUT_ERR("[create_videoout], not support p_cv_param->format !!!\n");
		return 0;
	}

	//now only  support yuv420 
	if (PF_YUV420 != p_cv_param->format){
		VIDEO_OUT_ERR("[create_videoout], not support p_cv_param->format !!!\n");
		p_cv_param->format = PF_YUV420;
	}

	pvideo = (p_video) malloc(sizeof(video));
	p_cv_param->pvideo = pvideo;
	if (pvideo){
		memset((void *) pvideo, 0, sizeof(video));

		irtn = init_video(type, pvideo, 3);
		if (irtn == 0){
			irtn = set_video_param(pvideo, p_cv_param);
			if (irtn != 0){
				VIDEO_OUT_ERR("set video param err \n");
				uninit_video(pvideo);
				free(pvideo);
				return 0;
			}

			init_video_buffer(pvideo);

			VIDEO_OUT_DBG("init_video_buffer over\n");

			if (p_cv_param->disp_left < 0 ||
					p_cv_param->disp_top <0 ||
					p_cv_param->disp_left >= SCREEN_WIDTH ||
					p_cv_param->disp_top>SCREEN_HEIGHT){
				VIDEO_OUT_DBG("p_cv_param not fit screen \n");
				set_position(pvideo, 0, 0);
			}else{
				set_position(pvideo, p_cv_param->disp_left, p_cv_param->disp_top);
			}

			qbuf_video(pvideo);
			enable_video(pvideo);

			VIDEO_OUT_DBG("pvideo (%d/%d/%d/%d)\n",
					pvideo->pixel_format,
					pvideo->win_x,
					pvideo->win_y,
					pvideo->bpp);           

			return (ULONG) pvideo;
		}else{
			uninit_video(pvideo);
			VIDEO_OUT_ERR("init video err\n");
			free(pvideo);
		}
	}
	return 0; //malloc fialed
}

void destroy_videoout(p_video pvideo)
{
	if (!pvideo){
		VIDEO_OUT_ERR("[destroy_videoout] in param video handle error\n");
		return ;
	}
	VIDEO_OUT_ERR("disable video\n");
	disable_video(pvideo);
	VIDEO_OUT_ERR("uninit video\n");
	uninit_video(pvideo);
	free(pvideo);
}

static int flip_video(p_video pvideo,char *p_y,char *p_u,char *p_v)
{
	int irtn;
	unsigned char * pframe;
	struct v4l2_buffer vbuffer;

	memset((void *) &vbuffer, 0, sizeof(struct v4l2_buffer));

	pvideo->index++;

	pvideo->index %= pvideo->buf_count;

	VIDEO_OUT_DBG("pvideo->index %d pframe 0x%d src_x:%d src_y:%d win_x:%d, win_y:%d\n ",
			pvideo->index,
			(int)
			pframe,
			pvideo->src_x,
			pvideo->src_y,
			pvideo->win_x,
			pvideo->win_y);

	pframe = pvideo->buff[pvideo->index];

	memcpy(pframe, p_y, (pvideo->src_x * pvideo->src_y));
	pframe += (pvideo->src_x * pvideo->src_y);

	memcpy(pframe, p_u, (pvideo->src_x * pvideo->src_y) / 4);
	pframe += (pvideo->src_x * pvideo->src_y) / 4;

	memcpy(pframe, p_v, (pvideo->src_x * pvideo->src_y) / 4);  

	vbuffer.index = pvideo->index;

	vbuffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	vbuffer.memory = V4L2_MEMORY_MMAP;

	vbuffer.m.offset = pvideo->offsett[pvideo->index];

	return ioctl(pvideo->idev, VIDIOC_QBUF, &vbuffer);
}


void display_video(ULONG h_videoout,char *p_y,char *p_u,char *p_v)
{
	p_video pvideo = (p_video) h_videoout;

	if (!h_videoout || (!p_y) || (!p_u) || (!p_v)){
		VIDEO_OUT_ERR("h_videoout[%p],p_y[%p],p_u[%p],p_v [%p]\n",h_videoout,p_y,p_u,p_v);
		VIDEO_OUT_ERR("[display_video] in param video handle error\n");
		return ;
	}

	//set_position(pvideo, pvideo->left, pvideo->top);      //mask, and v4L2 driver fixed this bug 
	flip_video(pvideo, p_y, p_u, p_v);
}


unsigned long buffer_count;
int src = 0;
unsigned long buffer_count;
unsigned int win_x,win_y,win_w,win_h;
unsigned long width, height;
int  mclk = 24;
#define CLEAR(x) memset (&(x), 0, sizeof (x))

#define DL_CMIF_U_OFFSET(y_addr,w,h) (unsigned int)((y_addr)+((w)*(h)))
#define DL_CMIF_V_OFFSET(y_addr,w,h) (unsigned int)((y_addr)+((w)*(h)/4)*5)


#define container_of(ptr, type, member) ( \
		const typeof( ((type *)0)->member ) *__mptr = (ptr); \
		(type *)( (char *)__mptr - offsetof(type,member) ); ) 

#define list_entry(ptr, type, member) container_of(ptr, type, member) 



struct list_head { 
	struct list_head *next, *prev; 
}; 


enum{
	UC13X_CTRL_STREAM_PRIV = 0x1000,
	UC13X_CTRL_STREAM_ZOOM,
} ;

typedef enum{
	IO_METHOD_READ,
	IO_METHOD_MMAP,
	IO_METHOD_USERPTR,
} io_method;

struct buffer_t{
	void *start;
	size_t length;
};

static char dev_name[20] = "/dev/video100";
static io_method io = IO_METHOD_MMAP;
static int fd = -1;
struct buffer_t buffers[32];
static unsigned int n_buffers = 0;

static unsigned long video_handle;






static void errno_exit(const char *s)
{
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));

	exit(EXIT_FAILURE);
}

static int xioctl(int fd,int request,void *arg)
{
	int r;
	r = ioctl(fd, request, arg);
	while (-1 == r && EINTR == errno);
	return r;
}

int kbhit(void)
{
	struct termios oldt, newt;
	int ch;
	int oldf;
	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
	ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	fcntl(STDIN_FILENO, F_SETFL, oldf);
	if(ch != EOF)
	{
		ungetc(ch, stdin);
		return 1;
	}
	return 0;
}
static void stop_capturing(void);
static void process_image(const void *p, int id)
{
	unsigned int len = width * height* 3 / 2 ;
	unsigned int yuv_addr[3];

	struct v4l2_control control;
	struct uc13x_img_zoom zoom;
	control.id = UC13X_CTRL_STREAM_ZOOM;
	control.value = (int)(&zoom);
	zoom.src_index  = id;
	zoom.dst_index  = id;
	zoom.imageformat = 0;
	zoom.srcwidth = win_w;
	zoom.srcheight = win_h;
	zoom.dstwidth = width;
	zoom.dstheight = height;
	xioctl(fd, VIDIOC_S_CTRL, &control);
	if(src ==0) {
		yuv_addr[0] = (unsigned int) p;
		yuv_addr[1] = DL_CMIF_U_OFFSET(yuv_addr[0], width, height);
		yuv_addr[2] = DL_CMIF_V_OFFSET(yuv_addr[0], width, height);
		//printf("YUV addr is Y %x ,U %x,V %x\n", yuv_addr[0], yuv_addr[1], yuv_addr[2]);
		display_video(video_handle,
				(char *) yuv_addr[0],
				(char *) yuv_addr[1],
				(char *) yuv_addr[2]);
	}
	else{
		fputc('*', stdout);
		fflush(stdout);
		fwrite(p, sizeof(unsigned char), len, fyuv);
		fflush(fyuv);
	}

}


static int read_frame(void)
{
	struct v4l2_buffer buf;
	unsigned int i;
	switch (io){
		case IO_METHOD_MMAP:
			///fprintf(stderr,"IO_METHOD_MMAP\n");
			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)){
				switch (errno){
					case -EAGAIN:
						return 0;
					case -EIO:
						/* Could ignore EIO, see spec. */
						/* fall through */
					default:
						errno_exit("VIDIOC_DQBUF");
				}
			}
			assert(buf.index < n_buffers);

			process_image(buffers[buf.index].start,buf.index);
			if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");
			break;
	}
	return 1;
}
static void mainloop(void)
{
	enum v4l2_buf_type type;
	unsigned int count;
	char ch = 0;
	struct timeval start, stop;
	int diff;
	struct v4l2_control control;
	struct uc13x_img_zoom zoom;
	unsigned int len = width * height* 3 / 2 ;
	unsigned int yuv_addr[3];
	int frame_count = 0;
	camera_cfg_t cfg;
	FILE* fout;

	fd_set fds;
	struct timeval tv;
	int r;

	while(1){

		if(kbhit()){
			ch = getchar();
			if(ch == 'q'){
				printf("Key press 'q',Exit\n");
				break;	
			}else if(ch == 'p'){
				printf("Set QVGA\n");
				ch = getchar();
				//....
			}else if(ch == 'l'){
				printf("Set VGA\n");
				//...
			}

		}

		for (;;){
			/*TODO : comment: if the next paragram between #if 1 and #endif does not work, then
			 * the test pattern will always ended when calling VIDIOC_DQBUF iocontrol, although sometimes
			 * it can work for several hours. On the ohterwise, if the next paragram between #if 1 
			 * and #endif works, and if the  process_image function does not write image data to a
			 * file, then the test pattern can always being working rightly, but if writing data to a 
			 * file, then it can cause an usr_irq fault, maybe it's caused by the nfs, which needs to 
			 * verify latter. The next paragram before read_frame function is mainly used to call poll
			 * function*/

			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			/* Timeout. */
			tv.tv_sec = 20;
			tv.tv_usec = 0;
			r = select(fd + 1, &fds, NULL, NULL, &tv);
			if (-1 == r){
				if (EINTR == errno) {
					printf("main loop : EINTR == errno!\n");
					continue;
				}
				errno_exit("select");
			}
			if (0 == r){
				fprintf(stderr, "select timeout\n");
				exit(EXIT_FAILURE);
			}
			if (read_frame()){
				frame_count++;
				break;
			}

			/* EAGAIN - continue select loop. */
		}

	}
	cfg.fd = fd;
	cfg.videobuf_cnt = 0;
	cfg.x = 0;
	cfg.y = 0;
	cfg.w = SCREEN_WIDTH;
	cfg.h = SCREEN_HEIGHT;
	cfg.pic_w = 1024;
	cfg.pic_h = 768;
	camera_zoom_img(fd,1,2,&cfg);
	fout = fopen("/mnt/udisk/vga.yuv","wb");
	if(fout ==NULL){
		printf("can't open log file\n");
		return;
	}
	fwrite(buffers[1].start,1,SCREEN_WIDTH*SCREEN_HEIGHT*3/2, fout);
	fclose(fout);

	fout = fopen("/mnt/udisk/svga.yuv","wb");
	if(fout ==NULL){
		printf("can't open log file\n");
		return;
	}
	fwrite(buffers[2].start,1,1024*768*3/2, fout);
	fclose(fout);


}
static void dqbuf(void){
	struct v4l2_buffer buf;

	for (;;){
		fd_set fds;
		struct timeval tv;
		int r;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		/* Timeout. */
		tv.tv_sec = 20;
		tv.tv_usec = 0;
		r = select(fd + 1, &fds, NULL, NULL, &tv);
		if (-1 == r){
			if (EINTR == errno)
				continue;
			errno_exit("select");
		}
		if (0 == r){
			fprintf(stderr, "select timeout\n");
			exit(EXIT_FAILURE);
		}


		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)){
			switch (errno){
				case -EAGAIN:
					continue;
				case -EIO:
					/* Could ignore EIO, see spec. */
					/* fall through */
				default:
					errno_exit("VIDIOC_DQBUF");
			}
		}

		break;

		/* EAGAIN - continue select loop. */
	}

}

static void stop_capturing(void)
{
	enum v4l2_buf_type type;
	fprintf(stderr, "stop_capturing\n");
	switch (io){
		case IO_METHOD_READ:
			/* Nothing to do. */
			break;

		case IO_METHOD_MMAP:
		case IO_METHOD_USERPTR:
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
				errno_exit("VIDIOC_STREAMOFF");
			break;
	}
}

static void start_capturing(void)
{
	unsigned int i;
	enum v4l2_buf_type type;

	fprintf(stderr, "start_capturing\n");
	switch (io){
		case IO_METHOD_READ:
			/* Nothing to do. */    
			fprintf(stderr, "IO_METHOD_READ\n");
			break;
		case IO_METHOD_MMAP:
			fprintf(stderr, "IO_METHOD_MMAP\n");
			for (i = 0;i < n_buffers;++i){
				struct v4l2_buffer buf;
				CLEAR(buf);
				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_MMAP;
				buf.index = i;
				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
					errno_exit("VIDIOC_QBUF");
				}
			}
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

			if (-1 == xioctl(fd, VIDIOC_STREAMON, &type)) {
				errno_exit("VIDIOC_STREAMON");
			}
			break;
		case IO_METHOD_USERPTR:
			fprintf(stderr, "IO_METHOD_USERPTR");
			for (i = 0;i < n_buffers;++i){
				struct v4l2_buffer buf;
				CLEAR(buf);
				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_USERPTR;
				buf.index = i;
				buf.m.userptr = (unsigned long) buffers[i].start;
				buf.length = buffers[i].length;
				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
					errno_exit("VIDIOC_QBUF");
			}
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
				errno_exit("VIDIOC_STREAMON");
			break;
	}
}

static void uninit_device(void)
{
	unsigned int i;
	fprintf(stderr, "uninit_device\n");
	switch (io){
		case IO_METHOD_READ:
			free(buffers[0].start);
			break;
		case IO_METHOD_MMAP:
			for (i = 0;i < n_buffers;++i)
				if (-1 == munmap(buffers[i].start, buffers[i].length))
					errno_exit("munmap");
			break;
		case IO_METHOD_USERPTR:
			for (i = 0;i < n_buffers;++i)
				free(buffers[i].start);
			break;
	}
}

static void init_read(unsigned int buffer_size)
{
	fprintf(stderr, "init_read\n");

	buffers[0].length = buffer_size;
	buffers[0].start = malloc(buffer_size);
	if (!buffers[0].start){
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
}

static void init_mmap(void)
{
	struct v4l2_requestbuffers req;
	CLEAR(req); 

	fprintf(stderr, "init_mmap\n");

	req.count = buffer_count;///4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)){
		if (EINVAL == errno){
			fprintf(stderr, "%s does not support "
					"memory mapping\n", dev_name);
			exit(EXIT_FAILURE);
		}else{
			errno_exit("VIDIOC_REQBUFS");
		}
	}   
#if 0
	if (req.count < 2) {
		fprintf (stderr, "Insufficient buffer memory on %s\n",
				dev_name);
		exit (EXIT_FAILURE);
	}
#endif
	for (n_buffers = 0;n_buffers < req.count;++n_buffers){
		struct v4l2_buffer buf;
		CLEAR(buf);
		printf("n_buffers value :%d\n",n_buffers);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;
		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
			errno_exit("VIDIOC_QUERYBUF");
		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = mmap(NULL /* start anywhere */,
				buf.length,
				PROT_READ | PROT_WRITE /* required */,
				MAP_SHARED /* recommended */,
				fd,
				buf.m.offset);
		fprintf(stderr,
				"buffers[%d].start=0x%x,length=0x%x\n",
				n_buffers,
				buffers[n_buffers].start,
				buf.length);
		if (MAP_FAILED == buffers[n_buffers].start)
			errno_exit("mmap");
	}
}

static void init_userp(unsigned int buffer_size)
{
	struct v4l2_requestbuffers req;
	unsigned int page_size;
	page_size = getpagesize();
	buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);
	CLEAR(req);

	fprintf(stderr, "init_userp\n");

	req.count = buffer_count;///4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;
	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)){
		if (EINVAL == errno){
			fprintf(stderr,
					"%s does not support "
					"user pointer i/o\n",
					dev_name);
			exit(EXIT_FAILURE);
		}else{
			errno_exit("VIDIOC_REQBUFS");
		}
	}
	if (!buffers){
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
	for (n_buffers = 0;n_buffers < buffer_count/*4*/;++n_buffers){
		buffers[n_buffers].length = buffer_size;
		buffers[n_buffers].start = memalign(/* boundary */page_size, buffer_size);
		if (!buffers[n_buffers].start){
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}
}

static void init_device(void)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	unsigned int min;
	int off;
	fprintf(stderr, "init_device\n");
	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)){
		if (EINVAL == errno){
			fprintf(stderr, "%s is no V4L2 device\n", dev_name);
			exit(EXIT_FAILURE);
		}else{
			errno_exit("VIDIOC_QUERYCAP");
		}
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)){
		fprintf(stderr, "%s is no video capture device\n", dev_name);
		exit(EXIT_FAILURE);
	}
#if 0
	switch (io){
		case IO_METHOD_READ:
			fprintf(stderr, "IO_METHOD_READ\n");
			if (!(cap.capabilities & V4L2_CAP_READWRITE)){
				fprintf(stderr, "%s does not support read i/o\n", dev_name);
				exit(EXIT_FAILURE);
			}
			break;
		case IO_METHOD_MMAP:
			fprintf(stderr, "IO_METHOD_MMAP111\n");
			break;
		case IO_METHOD_USERPTR:
			fprintf(stderr, "IO_METHOD_USERPTR****111\n");
			if (!(cap.capabilities & V4L2_CAP_STREAMING)){
				fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
				exit(EXIT_FAILURE);
			}
			break;
	}
#endif
	/* Select video input, video standard and tune here. */
	CLEAR(cropcap);
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)){
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c.left = win_x; 
		crop.c.top = win_y; 
		crop.c.width = win_w; 
		crop.c.height = win_h; 

		if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)){
			switch (errno){
				case -EINVAL:
					/* Cropping not supported. */
					break;
				default:
					/* Errors ignored. */
					break;
			}
		}
	}else{
		/* Errors ignored. */
	}
	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	fmt.fmt.pix.field =   /*V4L2_FIELD_INTERLACED*/V4L2_FIELD_ANY;
	fmt.fmt.pix.priv = mclk;
	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt)){
		errno_exit("VIDIOC_S_FMT");
	}
	/* Note VIDIOC_S_FMT may change width and height. */
	/* Buggy driver paranoia. */
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min){
		fmt.fmt.pix.bytesperline = min;
	}
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min){
		fmt.fmt.pix.sizeimage = min;
	}
	switch (io){
		case IO_METHOD_READ:
			fprintf(stderr, "IO_METHOD_READ\n");
			init_read(fmt.fmt.pix.sizeimage);
			break;
		case IO_METHOD_MMAP:
			fprintf(stderr, "IO_METHOD_MMAP\n");
			init_mmap();
			break;
		case IO_METHOD_USERPTR:
			fprintf(stderr, "IO_METHOD_USERPTR*******222\n");
			init_userp(fmt.fmt.pix.sizeimage);
			break;
	}
}
static void close_device(void)
{
	fprintf(stderr, "close_device\n");
	if (-1 == close(fd)){
		return;
	}
	fd = -1;
}

static void open_device(void)
{
	struct stat st;
	fprintf(stderr, "open_device\n");

	if (-1 == stat(dev_name, &st)){
		fprintf(stderr,
				"Cannot identify â€?sâ€? %d, %s\n",
				dev_name,
				errno,
				strerror(errno));

		exit(EXIT_FAILURE);
	}
	if (!S_ISCHR(st.st_mode)){
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);
	if (-1 == fd){
		fprintf(stderr,
				"Cannot open â€?sâ€? %d, %s\n",
				dev_name,
				errno,
				strerror(errno));
		exit(EXIT_FAILURE);
	}
}


void list_all_vodeo_device(void)
{
	int i;
	int pf = -1;
	struct v4l2_capability cap;

	char dvname[100];
	memset(dvname,0,100);
	if(!dvname){
		printf("can't get device name\n");
		return;
	}
	for(i = 0; i< 11 ; i ++){
		strcpy(dvname,"/dev/video");
		sprintf(dvname+strlen(dvname),"%d",i);
		printf("open device: [%s]\n",dvname);
		pf = open(dvname, O_RDWR /* required */ | O_NONBLOCK, 0);	
		if(pf==-1){
			printf("file can't open or file not exist\n");
			continue;
		}
		printf("opened file is 0x%.8lx\n",pf);

		memset(&cap, 0, sizeof(cap));

		if (-1 == xioctl(pf, VIDIOC_QUERYCAP, &cap)){
			if (EINVAL == errno){
				fprintf(stderr, "%s is no V4L2 device\n", dev_name);
				exit(EXIT_FAILURE);
			}
		}

		printf("-----------------------------------------------------\n");
		printf("[driver]:	0x%.8lx	0x%.8lx	0x%.8lx	0x%.8lx\n",
				*(unsigned int *)(cap.driver),
				*(unsigned int *)(cap.driver+4),
				*(unsigned int *)(cap.driver+8),
				*(unsigned int *)(cap.driver+12));
		printf("[card]:		0x%.8lx	0x%.8lx	0x%.8lx	 0x%.8lx\n",
				*(unsigned int *)(cap.card),
				*(unsigned int *)(cap.card+4),
				*(unsigned int *)(cap.card+8),
				*(unsigned int *)(cap.card+12));
		printf("		0x%.8lx	0x%.8lx	0x%.8lx	 0x%.8lx\n",
				*(unsigned int *)(cap.driver+16),
				*(unsigned int *)(cap.driver+20),
				*(unsigned int *)(cap.driver+24),
				*(unsigned int *)(cap.driver+28));
		printf("[capabilities]:	0x%.8lx\n",
				(unsigned int *)(cap.capabilities));
		printf("-----------------------------------------------------\n");
		printf("Close [%d]\n",close(pf));

	}	
}

/*./camera_test 0 0 320 240 320 240 0 \*/
int main(int argc,char **argv)
{
	cv_param video_param;
	int ret;
	void * tmpp;

	fyuv = NULL;
	if (argc <2){
		printf("usage: ./camera_test [buffer count]\n"
				"win_x     window x\n"
				"win_y     window y\n"
				"win_w     window width\n"
				"win_h     window height\n"
				"width    the width image\n"
				"height    the height image\n"
				"format    1 is to file 0 is to lcd\n"
				"device    /dev/videox\n"
				"-help     display this information\n");
		return 0;
	} 

	buffer_count = atoi(argv[1]);
	win_x = atoi(argv[2]);
	win_y = atoi(argv[3]);
	win_w = atoi(argv[4]);
	win_h = atoi(argv[5]);
	width = atoi(argv[6]);
	height = atoi(argv[7]);
	src = atoi(argv[8]);
	if (argv[9] != NULL){
		memset(dev_name, 0, sizeof(dev_name));	
		strncpy(dev_name, argv[9], strlen(argv[9]));
		printf("open device is %s\n", dev_name);
	}		

	video_param.format = PF_YUV420;
	video_param.width = width;
	video_param.height = height;
	video_param.disp_top = 0;
	video_param.disp_left = 0;
	video_param.disp_width = SCREEN_WIDTH;
	video_param.disp_height = SCREEN_HEIGHT;
	if(src == 1){
		printf("open log file\n");
		fyuv = fopen("./stream.yuv","wb");
		if(fyuv ==NULL){
			printf("can't open log file\n");
			return;
		}

	}else{
		video_handle = create_videoout(&video_param);
		if (video_handle == 0){
			fprintf(stderr, "Create video fail\n");
		}
	}

	io = IO_METHOD_MMAP;
	open_device();
	init_device();
	start_capturing();
	mainloop();
	fclose(fyuv);
	stop_capturing();
	//uninit_video(video_param.pvideo);
	//free(video_param.pvideo);
	destroy_videoout(video_param.pvideo);
	uninit_device();
	close_device();

	return 0;
}

