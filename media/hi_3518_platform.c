#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "hi_common.h"
#include "hi_comm_vb.h"
#include "mpi_sys.h"
#include "mpi_vb.h"
#include "hi_comm_sys.h"
#include "hi_comm_aio.h"
#include "acodec.h"
#include "hi_comm_adec.h"
#include "mpi_adec.h"
#include "mpi_ao.h"
#include "mpi_aenc.h"

#include "hi_3518_platform.h"

#define ACODEC_FILE     "/dev/acodec"

enum
{
	FAILURE = -1,
	SUCCESS = 0,
}yan_result;

int framework_init ()
{
	VB_CONF_S stVbConf;
	MPP_SYS_CONF_S stSysConf = {0};
	HI_S32 ret = HI_FAILURE;
	int result = FAILURE;

	HI_MPI_SYS_Exit ();
	HI_MPI_VB_Exit ();

	memset (&stVbConf, 0x00, sizeof (stVbConf));
	ret = HI_MPI_VB_SetConf (&stVbConf);
	if (ret != HI_SUCCESS)
	{
		printf ("HI_MPI_VB_SetConf Eorr[%x]\n", ret);
		return result;
	}

	ret = HI_MPI_VB_Init ();
	if (ret != HI_SUCCESS)
	{
		printf ("HI_MPI_VB_Init Eorr[%x]\n", ret);
		return result;
	}

	stSysConf.u32AlignWidth = 64;
	ret = HI_MPI_SYS_SetConf (&stSysConf);
	if (ret != HI_SUCCESS)
	{
		printf ("HI_MPI_SYS_SetConf Eorr[%x]\n", ret);
		return result;
	}

	ret = HI_MPI_SYS_Init ();
	if (ret != HI_SUCCESS)
	{
		printf ("HI_MPI_SYS_Init Eorr[%x]\n", ret);
		return result;
	}

	result = SUCCESS;
	return result;
}

int adec_ao (AIO_ATTR_S *pstAioAttr)
{
	HI_S32 		sret;
	int 		ret = FAILURE;
	AUDIO_DEV 	aoDev = 0;
	AO_CHN		aoChn = 0;
	ADEC_CHN	adChn = 0;
	int acodec_fd = 0;
	unsigned int i2s_fs_sel = 0;
	
	//-----------Set CFG-------------
	acodec_fd = open (ACODEC_FILE,O_RDWR);
	if (acodec_fd < 0)
	{
		printf ("open[%s] error[%s]\n", ACODEC_FILE, strerror (errno));
		return ret;
	}

	if (ioctl (acodec_fd, ACODEC_SOFT_RESET_CTRL))
	{
		printf ("reset codec error\n");
		return ret;
	}

	i2s_fs_sel = 0x18;//AUDIO_SAMPLE_RATE_8000

	if (ioctl (acodec_fd, ACODEC_SET_I2S1_FS,&i2s_fs_sel))
	{
		printf ("set acodec sample rate failure\n");
		return ret;
	}

	close (acodec_fd);

	//-------------Start Adec-------------
	ADEC_CHN_ATTR_S stAdecAttr;

	stAdecAttr.enType = PT_G711A;
	stAdecAttr.u32BufSize = 20;
	stAdecAttr.enMode = ADEC_MODE_STREAM;

	if (PT_ADPCMA == stAdecAttr.enType)
	{
		ADEC_ATTR_ADPCM_S stAdpcm;
		stAdecAttr.pValue = &stAdpcm;
		stAdpcm.enADPCMType = 0;
		printf ("-----------I do not Play Pcm\n");
	}
	else if (PT_G711A == stAdecAttr.enType || PT_G711U == stAdecAttr.enType)
	{
		ADEC_ATTR_G711_S stAdecG711;
		stAdecAttr.pValue = &stAdecG711;
	}
	else
	{
		printf ("Undefine Type\n");
		return ret;
	}

	sret = HI_MPI_ADEC_CreateChn(adChn, &stAdecAttr);
	if (sret != HI_SUCCESS)
	{
		printf ("HI_MPI_ADEC_CreateChn error[%x]\n", sret);
		return ret;
	}

	//----------------------Start Ao-----------
	sret = HI_MPI_AO_SetPubAttr (aoDev, pstAioAttr);
	if (sret != SUCCESS)
	{
		printf ("HI_MPI_AO_SetPubAttr error[%x]\n", sret);
		return ret;
	}

	sret = HI_MPI_AO_Enable (aoDev);
	if (sret != SUCCESS)
	{
		printf ("HI_MPI_AO_Enable error[%x]\n", sret);
		return ret;
	}

	sret = HI_MPI_AO_EnableChn (aoDev, aoChn);
	if (sret != SUCCESS)
	{
		printf ("HI_MPI_AO_EnableChn error[%x]\n", sret);
		return ret;
	}

	//-------- SAMPLE_COMM_AUDIO_AoBindAdec
	MPP_CHN_S stSrcChn,stDestChn;

	stSrcChn.enModId = HI_ID_ADEC;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = adChn;
	stDestChn.enModId = HI_ID_AO;
	stDestChn.s32DevId = aoDev;
	stDestChn.s32ChnId = aoChn;

	sret = HI_MPI_SYS_Bind (&stSrcChn, &stDestChn);
	if (sret != HI_SUCCESS)
	{
		printf ("HI_MPI_SYS_Bind error [%x]\n", sret);
		return ret;
	}
	printf ("HI_MPI_SYS_Bind success\n");

	//--------------SAMPLE_COMM_AUDIO_AdecProc
	int AdChn = adChn;
	FILE *pfd = NULL;
	AUDIO_STREAM_S stAudioStream;
	HI_U32 u32Len = 324;
	HI_U32 u32ReadLen;
	HI_S32 s32AdecChn = AdChn;
	HI_U8 *pu8AudioStream = NULL;

	pfd = fopen ("/root/ap_down.voice", "r");
	if (pfd == NULL)
	{
		printf ("open file error[%s]\n", strerror (errno));
		return -1;
	}
	printf ("open file success\n");
	pu8AudioStream = (HI_U8*)malloc(sizeof(HI_U8)*MAX_AUDIO_STREAM_LEN);

	while (1)
	{
		stAudioStream.pStream = pu8AudioStream;
		u32ReadLen = fread(stAudioStream.pStream, 1, u32Len, pfd/*文件FD*/);
		if (u32ReadLen <= 0)
		{
			printf ("read complete\n");
			break;
		}
		stAudioStream.u32Len = u32ReadLen; 
		sret = HI_MPI_ADEC_SendStream(s32AdecChn, &stAudioStream, HI_TRUE);
		if (sret != HI_SUCCESS)
		{
			printf ("HI_MPI_ADEC_SendStream error [%x]\n", sret);
			break;
		}
	}
	free(pu8AudioStream);
	fclose(pfd);

	printf ("Wow, it ok\n");
	return 0;
}

int test_platfore (int test)
{
	int ret = FAILURE;
	AIO_ATTR_S stAioAttr;
	
	ret = framework_init ();
	if (ret != SUCCESS)
	{
		printf ("framework_init error\n");
		return FAILURE;
	}

	printf ("framework_init success\n");

	memset (&stAioAttr, 0x00, sizeof (stAioAttr));
	stAioAttr.enSamplerate = AUDIO_SAMPLE_RATE_8000;
	stAioAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	stAioAttr.enWorkmode = AIO_MODE_I2S_MASTER;
	stAioAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
	stAioAttr.u32EXFlag = 1;
	stAioAttr.u32FrmNum = 30;
	stAioAttr.u32PtNumPerFrm = 320;
	stAioAttr.u32ChnCnt = 2;
	stAioAttr.u32ClkSel = 1;

	ret = adec_ao (&stAioAttr);
	if (ret != SUCCESS)
	{
		printf ("adec_ao error\n");
		return FAILURE;
	}
	printf ("adec_ao Success\n");

	return SUCCESS;
}
