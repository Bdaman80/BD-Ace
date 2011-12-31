/* Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora Forum nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * Alternatively, provided that this notice is retained in full, this software
 * may be relicensed by the recipient under the terms of the GNU General Public
 * License version 2 ("GPL") and only version 2, in which case the provisions of
 * the GPL apply INSTEAD OF those given above.  If the recipient relicenses the
 * software under the GPL, then the identification text in the MODULE_LICENSE
 * macro must be changed to reflect "GPLv2" instead of "Dual BSD/GPL".  Once a
 * recipient changes the license terms to the GPL, subsequent recipients shall
 * not relicense under alternate licensing terms, including the BSD or dual
 * BSD/GPL terms.  In addition, the following license statement immediately
 * below and between the words START and END shall also then apply when this
 * software is relicensed under the GPL:
 *
 * START
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 and only version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * END
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/msm_audio_7X30.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <mach/qdsp5v2_1x/audio_dev_ctl.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <mach/debug_audio_mm.h>
#include <mach/qdsp5v2_1x/qdsp5audppmsg.h>
#include <mach/qdsp5v2_1x/audpp.h>

#include <mach/qdsp5v2/snddev_icodec.h>
#include <linux/mfd/msm-adie-codec.h>


#ifndef MAX
#define  MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif


static DEFINE_MUTEX(session_lock);

struct audio_dev_ctrl_state {
	struct msm_snddev_info *devs[AUDIO_DEV_CTL_MAX_DEV];
	u32 num_dev;
	atomic_t opened;
	struct msm_snddev_info *voice_rx_dev;
	struct msm_snddev_info *voice_tx_dev;
/*   	wait_queue_head_t      wait;  */
};

static struct audio_dev_ctrl_state audio_dev_ctrl;
struct event_listner event;
#define MAX_DEC_SESSIONS	6
#define MAX_ENC_SESSIONS	2

struct session_freq {
	int freq;
	int evt;
};


#undef REC_TEST

#ifdef REC_TEST
struct rt_data {
     uint32_t enabled_devs;	
     struct msm_snddev_info voice_rx_dev;
     struct msm_snddev_info voice_tx_dev;
     int rx_valid;
     int tx_valid;
};
#endif

struct audio_routing_info {
	unsigned short mixer_mask[MAX_DEC_SESSIONS];
	unsigned short audrec_mixer_mask[MAX_ENC_SESSIONS];
	struct session_freq dec_freq[MAX_DEC_SESSIONS];
	struct session_freq enc_freq[MAX_ENC_SESSIONS];
	int voice_tx_dev_id;
	int voice_rx_dev_id;
	int voice_tx_sample_rate;
	int voice_rx_sample_rate;
	signed int voice_tx_vol;
	signed int voice_rx_vol;
	int tx_mute;
	int rx_mute;
	int voice_state;
};

static struct audio_routing_info routing_info;

int msm_get_voice_state(void)
{
        MM_DBG("voice state %d\n", routing_info.voice_state);
        return routing_info.voice_state;
}
EXPORT_SYMBOL(msm_get_voice_state);

int msm_set_voice_mute(int dir, int mute)
{
	MM_AUD_INFO("dir %x mute %x\n", dir, mute);
        if (!audio_dev_ctrl.voice_rx_dev
                || !audio_dev_ctrl.voice_tx_dev)
                return -EPERM;
	if (dir == DIR_TX) {
		routing_info.tx_mute = mute;
		broadcast_event(AUDDEV_EVT_DEVICE_VOL_MUTE_CHG,
			routing_info.voice_tx_dev_id, SESSION_IGNORE);
	} else{
		routing_info.rx_mute = mute;
		pr_aud_info("%s, rx_mute=%d\n", __func__, routing_info.rx_mute);
		broadcast_event(AUDDEV_EVT_DEVICE_VOL_MUTE_CHG,
			routing_info.voice_rx_dev_id, SESSION_IGNORE);
	}
	return 0;
}
EXPORT_SYMBOL(msm_set_voice_mute);

int msm_set_voice_vol(int dir, s32 volume)
{
        if (!audio_dev_ctrl.voice_rx_dev
                || !audio_dev_ctrl.voice_tx_dev)
                return -EPERM;
	MM_DBG("dir %d, vol %d\n", dir, volume);
	if (dir == DIR_TX) {
		routing_info.voice_tx_vol = volume;
		broadcast_event(AUDDEV_EVT_DEVICE_VOL_MUTE_CHG,
					routing_info.voice_tx_dev_id,
					SESSION_IGNORE);
	} else if (dir == DIR_RX) {
		routing_info.voice_rx_vol = volume;
		broadcast_event(AUDDEV_EVT_DEVICE_VOL_MUTE_CHG,
					routing_info.voice_rx_dev_id,
					SESSION_IGNORE);
	} else
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL(msm_set_voice_vol);

void msm_snddev_register(struct msm_snddev_info *dev_info)
{
	MM_AUD_INFO("msm_snddev_register\n");
	mutex_lock(&session_lock);
	if (audio_dev_ctrl.num_dev < AUDIO_DEV_CTL_MAX_DEV) {
		audio_dev_ctrl.devs[audio_dev_ctrl.num_dev] = dev_info;
		dev_info->dev_volume = 0; /* 0 db */
		dev_info->sessions = 0x0;
		dev_info->set_sample_rate = 0;
		audio_dev_ctrl.num_dev++;
	} else
		MM_AUD_ERR("%s: device registry max out\n", __func__);
	mutex_unlock(&session_lock);
}
EXPORT_SYMBOL(msm_snddev_register);

int msm_snddev_devcount(void)
{
	return audio_dev_ctrl.num_dev;
}
EXPORT_SYMBOL(msm_snddev_devcount);

int msm_snddev_query(int dev_id)
{
	if (dev_id <= audio_dev_ctrl.num_dev)
			return 0;
	return -ENODEV;
}
EXPORT_SYMBOL(msm_snddev_query);

int msm_snddev_is_set(int popp_id, int copp_id)
{
	MM_DBG("popp=%d copp=%d\n", popp_id, copp_id);
	return routing_info.mixer_mask[popp_id] & (0x1 << copp_id);
}
EXPORT_SYMBOL(msm_snddev_is_set);

unsigned short msm_snddev_route_enc(int enc_id)
{
	MM_DBG("enc=%d\n", enc_id);
	if (enc_id >= MAX_ENC_SESSIONS)
		return -EINVAL;
	return routing_info.audrec_mixer_mask[enc_id];
}
EXPORT_SYMBOL(msm_snddev_route_enc);

unsigned short msm_snddev_route_dec(int popp_id)
{
	if (popp_id >= MAX_DEC_SESSIONS)
		return -EINVAL;
	return routing_info.mixer_mask[popp_id];
}
EXPORT_SYMBOL(msm_snddev_route_dec);

int msm_snddev_set_dec(int popp_id, int copp_id, int set)
{
	if (set)
		routing_info.mixer_mask[popp_id] |= (0x1 << copp_id);
	else
		routing_info.mixer_mask[popp_id] &= ~(0x1 << copp_id);

	return 0;
}
EXPORT_SYMBOL(msm_snddev_set_dec);

int msm_snddev_set_enc(int popp_id, int copp_id, int set)
{
	MM_DBG("popp=%d, copp=%d, set=%d\n", popp_id, copp_id, set);
	if (set)
		routing_info.audrec_mixer_mask[popp_id] |= (0x1 << copp_id);
	else
		routing_info.audrec_mixer_mask[popp_id] &= ~(0x1 << copp_id);
	return 0;
}
EXPORT_SYMBOL(msm_snddev_set_enc);

int msm_device_is_voice(int dev_id)
{
	if ((dev_id == routing_info.voice_rx_dev_id)
		|| (dev_id == routing_info.voice_tx_dev_id))
		return 0;
	else
		return -EINVAL;
}
EXPORT_SYMBOL(msm_device_is_voice);

int msm_set_voc_route(struct msm_snddev_info *dev_info,
			int stream_type, int dev_id)
{
	int rc = 0;
	u32 session_mask = 0;

	if (dev_info == NULL) {
		MM_AUD_ERR("%s: invalid device info\n", __func__);
		return -EINVAL;
	}
	MM_AUD_INFO("msm_set_voc_route: %s stream_type %d dev_id %d\n", dev_info->name, stream_type, dev_id);
	mutex_lock(&session_lock);
	switch (stream_type) {
	case AUDIO_ROUTE_STREAM_VOICE_RX:
		if (audio_dev_ctrl.voice_rx_dev)
			audio_dev_ctrl.voice_rx_dev->sessions &= ~0xFF;

		if (!(dev_info->capability & SNDDEV_CAP_RX) |
		    !(dev_info->capability & SNDDEV_CAP_VOICE)) {
			rc = -EINVAL;
			break;
		}
		audio_dev_ctrl.voice_rx_dev = dev_info;
		if (audio_dev_ctrl.voice_rx_dev) {
			session_mask =
				0x1 << (8 * ((int)AUDDEV_CLNT_VOC-1));
			audio_dev_ctrl.voice_rx_dev->sessions |=
				session_mask;
		}
		routing_info.voice_rx_dev_id = dev_id;
		break;
	case AUDIO_ROUTE_STREAM_VOICE_TX:
		if (audio_dev_ctrl.voice_tx_dev)
			audio_dev_ctrl.voice_tx_dev->sessions &= ~0xFF;

		if (!(dev_info->capability & SNDDEV_CAP_TX) |
		    !(dev_info->capability & SNDDEV_CAP_VOICE)) {
			rc = -EINVAL;
			break;
		}

		audio_dev_ctrl.voice_tx_dev = dev_info;
		if (audio_dev_ctrl.voice_rx_dev) {
			session_mask =
				0x1 << (8 * ((int)AUDDEV_CLNT_VOC-1));
			audio_dev_ctrl.voice_tx_dev->sessions |=
				session_mask;
		}
		routing_info.voice_tx_dev_id = dev_id;
		break;
	default:
		rc = -EINVAL;
	}
	mutex_unlock(&session_lock);
	return rc;
}
EXPORT_SYMBOL(msm_set_voc_route);

/* Bullshit.
void msm_release_voc_thread(void)
{
	wake_up(&audio_dev_ctrl.wait);
}
EXPORT_SYMBOL(msm_release_voc_thread);
*/

int msm_snddev_get_enc_freq(session_id)
{
	return routing_info.enc_freq[session_id].freq;
}
EXPORT_SYMBOL(msm_snddev_get_enc_freq);

int msm_get_voc_freq(int *tx_freq, int *rx_freq)
{
	*tx_freq = routing_info.voice_tx_sample_rate;
	*rx_freq = routing_info.voice_rx_sample_rate;
	return 0;
}
EXPORT_SYMBOL(msm_get_voc_freq);

int msm_get_voc_route(u32 *rx_id, u32 *tx_id)
{
	int rc = 0;

	if (!rx_id || !tx_id)
		return -EINVAL;

	mutex_lock(&session_lock);
	if (!audio_dev_ctrl.voice_rx_dev || !audio_dev_ctrl.voice_tx_dev) {
		rc = -ENODEV;
		mutex_unlock(&session_lock);
		return rc;
	}

	*rx_id = audio_dev_ctrl.voice_rx_dev->acdb_id;
	*tx_id = audio_dev_ctrl.voice_tx_dev->acdb_id;
	MM_DBG("voc route rx=%d tx=%d", *rx_id, *tx_id);
	mutex_unlock(&session_lock);

	return rc;
}
EXPORT_SYMBOL(msm_get_voc_route);

#ifdef REC_TEST
struct msm_snddev_info *msm_get_tx_voc_route(void)
{
	return audio_dev_ctrl.voice_tx_dev;
}
EXPORT_SYMBOL(msm_get_tx_voc_route);
#endif

struct msm_snddev_info *audio_dev_ctrl_find_dev(u32 dev_id)
{
	struct msm_snddev_info *info;
	MM_DBG("dev=%d\n", dev_id);
	if ((audio_dev_ctrl.num_dev - 1) < dev_id) {
		info = ERR_PTR(-ENODEV);
		goto error;
	}

	info = audio_dev_ctrl.devs[dev_id];
error:
	return info;

}
EXPORT_SYMBOL(audio_dev_ctrl_find_dev);

int snddev_voice_set_volume(int vol, int path)
{
	if (audio_dev_ctrl.voice_rx_dev
		&& audio_dev_ctrl.voice_tx_dev) {
		if (path)
			audio_dev_ctrl.voice_tx_dev->dev_volume = vol;
		else
			audio_dev_ctrl.voice_rx_dev->dev_volume = vol;
	} else
		return -ENODEV;
	return 0;
}
EXPORT_SYMBOL(snddev_voice_set_volume);

static int audio_dev_ctrl_get_devices(struct audio_dev_ctrl_state *dev_ctrl,
				      void __user *arg)
{
	int rc = 0;
	u32 index;
	struct msm_snd_device_list work_list;
	struct msm_snd_device_info *work_tbl;

	if (copy_from_user(&work_list, arg, sizeof(work_list))) {
		rc = -EFAULT;
		goto error;
	}

	if (work_list.num_dev > dev_ctrl->num_dev) {
		rc = -EINVAL;
		goto error;
	}

	work_tbl = kmalloc(work_list.num_dev *
		sizeof(struct msm_snd_device_info), GFP_KERNEL);
	if (!work_tbl) {
		rc = -ENOMEM;
		goto error;
	}

	for (index = 0; index < dev_ctrl->num_dev; index++) {
		work_tbl[index].dev_id = index;
		work_tbl[index].dev_cap = dev_ctrl->devs[index]->capability;
		strlcpy(work_tbl[index].dev_name, dev_ctrl->devs[index]->name,64);
	}

	if (copy_to_user((void *) (work_list.list), work_tbl,
		 work_list.num_dev * sizeof(struct msm_snd_device_info)))
		rc = -EFAULT;
	kfree(work_tbl);
error:
	return rc;
}


int auddev_register_evt_listner(u32 evt_id, u32 clnt_type, u32 clnt_id,
		void (*listner)(u32 evt_id,
			union auddev_evt_data *evt_payload,
			void *private_data),
		void *private_data)
{
	int rc;
	struct msm_snd_evt_listner *callback = NULL;
	struct msm_snd_evt_listner *new_cb;

	new_cb = kzalloc(sizeof(struct msm_snd_evt_listner), GFP_KERNEL);
	if (!new_cb) {
		MM_AUD_ERR("No memory to add new listener node\n");
		return -ENOMEM;
	}
	MM_AUD_INFO("evt_id %x clnt_type %d clnt_id %d\n", evt_id, clnt_type, clnt_id);
	mutex_lock(&session_lock);
	new_cb->cb_next = NULL;
	new_cb->auddev_evt_listener = listner;
	new_cb->evt_id = evt_id;
	new_cb->clnt_type = clnt_type;
	new_cb->clnt_id = clnt_id;
	new_cb->private_data = private_data;
	if (event.cb == NULL) {
		event.cb = new_cb;
		new_cb->cb_prev = NULL;
	} else {
		callback = event.cb;
		for (; ;) {
			if (callback->cb_next == NULL)
				break;
			else {
				callback = callback->cb_next;
				continue;
			}
		}
		callback->cb_next = new_cb;
		new_cb->cb_prev = callback;
	}
	event.num_listner++;
	mutex_unlock(&session_lock);
	rc = 0;
	return rc;
}
EXPORT_SYMBOL(auddev_register_evt_listner);

int auddev_unregister_evt_listner(u32 clnt_type, u32 clnt_id)
{
	struct msm_snd_evt_listner *callback = event.cb;
	struct msm_snddev_info *info;
	u32 session_mask = 0;
	int i = 0;

	mutex_lock(&session_lock);
	while (callback != NULL) {
		if ((callback->clnt_type == clnt_type)
			&& (callback->clnt_id == clnt_id))
			break;
		 callback = callback->cb_next;
	}
	if (callback == NULL) {
		mutex_unlock(&session_lock);
		return -EINVAL;
	}

	if ((callback->cb_next == NULL) && (callback->cb_prev == NULL))
		event.cb = NULL;
	else if (callback->cb_next == NULL)
		callback->cb_prev->cb_next = NULL;
	else if (callback->cb_prev == NULL) {
		callback->cb_next->cb_prev = NULL;
		event.cb = callback->cb_next;
	} else {
		callback->cb_prev->cb_next = callback->cb_next;
		callback->cb_next->cb_prev = callback->cb_prev;
	}
	kfree(callback);

	session_mask = (0x1 << (clnt_id)) << (8 * ((int)clnt_type-1));
	for (i = 0; i < audio_dev_ctrl.num_dev; i++) {
		info = audio_dev_ctrl.devs[i];
		info->sessions &= ~session_mask;
	}
	mutex_unlock(&session_lock);
	return 0;
}
EXPORT_SYMBOL(auddev_unregister_evt_listner);

int msm_snddev_withdraw_freq(u32 session_id, u32 capability, u32 clnt_type)
{
	int i = 0;
	struct msm_snddev_info *info;
	u32 session_mask = 0;

	MM_DBG("\n");
	if ((clnt_type == AUDDEV_CLNT_VOC) && (session_id != 0))
		return -EINVAL;
	if ((clnt_type == AUDDEV_CLNT_DEC)
			&& (session_id >= MAX_DEC_SESSIONS))
		return -EINVAL;
	if ((clnt_type == AUDDEV_CLNT_ENC)
			&& (session_id >= MAX_ENC_SESSIONS))
		return -EINVAL;

	session_mask = (0x1 << (session_id)) << (8 * ((int)clnt_type-1));

	for (i = 0; i < audio_dev_ctrl.num_dev; i++) {
		info = audio_dev_ctrl.devs[i];
		if ((info->sessions & session_mask)
			&& (info->capability & capability)) {
			if (!(info->sessions & ~(session_mask)))
				info->set_sample_rate = 0;
		}
	}
	if (clnt_type == AUDDEV_CLNT_DEC)
		routing_info.dec_freq[session_id].freq
					= 0;
	else if (clnt_type == AUDDEV_CLNT_ENC)
		routing_info.enc_freq[session_id].freq
					= 0;
	else if (capability == SNDDEV_CAP_TX)
		routing_info.voice_tx_sample_rate = 0;
	else
		routing_info.voice_rx_sample_rate = 48000;
	return 0;
}
EXPORT_SYMBOL(msm_snddev_withdraw_freq);

int msm_snddev_request_freq(int *freq, u32 session_id,
			u32 capability, u32 clnt_type)
{
	int i = 0;
	int rc = 0;
	struct msm_snddev_info *info;
	u32 set_freq;
	u32 session_mask = 0;
	u32 clnt_type_mask = 0;

	MM_DBG(": clnt_type 0x%08x\n", clnt_type);

	if ((clnt_type == AUDDEV_CLNT_VOC) && (session_id != 0))
		return -EINVAL;
	if ((clnt_type == AUDDEV_CLNT_DEC)
			&& (session_id >= MAX_DEC_SESSIONS))
		return -EINVAL;
	if ((clnt_type == AUDDEV_CLNT_ENC)
			&& (session_id >= MAX_ENC_SESSIONS))
		return -EINVAL;
	session_mask = ((0x1 << session_id)) << (8 * (clnt_type-1));
	clnt_type_mask = (0xFF << (8 * (clnt_type-1)));
	if (!(*freq == 8000) && !(*freq == 11025) &&
		!(*freq == 12000) && !(*freq == 16000) &&
		!(*freq == 22050) && !(*freq == 24000) &&
		!(*freq == 32000) && !(*freq == 44100) &&
		!(*freq == 48000))
		return -EINVAL;

	for (i = 0; i < audio_dev_ctrl.num_dev; i++) {
		info = audio_dev_ctrl.devs[i];
		if ((info->sessions & session_mask)
			&& (info->capability & capability)) {
			rc = 0;
			if ((info->sessions & ~clnt_type_mask)
				&& ((*freq != 8000) && (*freq != 16000)
					&& (*freq != 48000))) {
				if (clnt_type == AUDDEV_CLNT_ENC) {
					routing_info.enc_freq[session_id].freq
							= 0;
					return -EPERM;
				} else if (clnt_type == AUDDEV_CLNT_DEC) {
					routing_info.dec_freq[session_id].freq
							= 0;
					return -EPERM;
				}
			}
			if (*freq == info->set_sample_rate) {
				rc = info->set_sample_rate;
				continue;
			}
			set_freq = MAX(*freq, info->set_sample_rate);
			if (set_freq == info->set_sample_rate) {
				rc = info->set_sample_rate;
				*freq = info->set_sample_rate;
				continue;
			}

			if (clnt_type == AUDDEV_CLNT_DEC) {
				routing_info.dec_freq[session_id].evt = 1;
				routing_info.dec_freq[session_id].freq
						= set_freq;
			} else if (clnt_type == AUDDEV_CLNT_ENC) {
				routing_info.enc_freq[session_id].evt = 1;
				routing_info.enc_freq[session_id].freq
						= set_freq;
			} else if (capability == SNDDEV_CAP_TX)
				routing_info.voice_tx_sample_rate = set_freq;

			rc = set_freq;
			info->set_sample_rate = set_freq;
			*freq = info->set_sample_rate;

			if (info->opened) {
				broadcast_event(AUDDEV_EVT_FREQ_CHG, i,
							SESSION_IGNORE);
				set_freq = info->dev_ops.set_freq(info,
								set_freq);
				broadcast_event(AUDDEV_EVT_DEV_RDY, i,
							SESSION_IGNORE);
			}
		}
	/*	MM_DBG("info->set_sample_rate = %d\n", info->set_sample_rate);
		MM_DBG("routing_info.enc_freq.freq = %d\n", routing_info.enc_freq[session_id].freq); */
	}
	return rc;
}
EXPORT_SYMBOL(msm_snddev_request_freq);

int msm_snddev_enable_sidetone(u32 dev_id, u32 enable)
{
	int rc;
	struct msm_snddev_info *dev_info;

	MM_DBG("dev_id %d enable %d\n", dev_id, enable);

	dev_info = audio_dev_ctrl_find_dev(dev_id);

	if (IS_ERR(dev_info)) {
		MM_AUD_ERR("bad dev_id %d\n", dev_id);
		rc = -EINVAL;
	} else if (!dev_info->dev_ops.enable_sidetone) {
		MM_DBG("dev %d no sidetone support\n", dev_id);
		rc = -EPERM;
	} else
		rc = dev_info->dev_ops.enable_sidetone(dev_info, enable);

	return rc;
}
EXPORT_SYMBOL(msm_snddev_enable_sidetone);

static int audio_dev_ctrl_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	struct audio_dev_ctrl_state *dev_ctrl = file->private_data;

	mutex_lock(&session_lock);
	switch (cmd) {
	case AUDIO_GET_NUM_SND_DEVICE:
		MM_DBG("AUDIO_GET_NUM_SND_DEVICE\n");
		rc = put_user(dev_ctrl->num_dev, (uint32_t __user *) arg);
		break;
	case AUDIO_GET_SND_DEVICES:
		MM_DBG("AUDIO_GET_SND_DEVICES\n");
		rc = audio_dev_ctrl_get_devices(dev_ctrl, (void __user *) arg);
		break;
	case AUDIO_ENABLE_SND_DEVICE: {
		struct msm_snddev_info *dev_info;
		u32 dev_id;

		if (get_user(dev_id, (u32 __user *) arg)) {
			MM_ERR("AUDIO_ENABLE_SND_DEVICE: cannot get dev_it\n");	
			rc = -EFAULT;
			break;
		}
		MM_DBG("AUDIO_ENABLE_SND_DEVICE %d: entry\n", dev_id);
		dev_info = audio_dev_ctrl_find_dev(dev_id);
		if (IS_ERR(dev_info)) rc = PTR_ERR(dev_info);
		else if (!dev_info->opened) {
		    int retries = 3;	
#ifdef REC_TEST
		    uint32_t set_freq = 0; //dev_info->sample_rate;
		    int rx_freq = 0, tx_freq = 0;

			if(msm_device_is_voice(dev_id) == 0) {
				MM_DBG("setting freq\n");
				 msm_get_voc_freq(&tx_freq, &rx_freq);
                		if (dev_info->capability & SNDDEV_CAP_TX) set_freq = tx_freq;
			//	else if (dev_info->capability & SNDDEV_CAP_RX) set_freq = rx_freq;
                                if (set_freq == 0) set_freq = dev_info->sample_rate;
			} else set_freq = dev_info->sample_rate;
			rc = dev_info->dev_ops.set_freq(dev_info, set_freq);
			MM_DBG("set_freq=%d freq=%d\n", set_freq, rc);
			if (rc < 0) {
				MM_DBG("device freq failed!\n");
				break;
			}
			dev_info->set_sample_rate = rc;
			rc = 0;
#endif
                        do {
			rc = dev_info->dev_ops.open(dev_info);
                                retries--;
				if(rc) MM_DBG("open(dev_info) error %d, retrying\n", rc);
                        } while (rc < 0 && retries);
			if (!rc) {
				dev_info->opened = 1;
#if 0 //def REC_TEST 
				mutex_unlock(&session_lock);
				broadcast_event(AUDDEV_EVT_DEV_RDY, dev_id, SESSION_IGNORE);
				MM_DBG("AUDIO_ENABLE_SND_DEVICE: done, dev=%d (%s)\n", dev_id, dev_info->name);
				return rc;
#endif
		}
		} else MM_DBG("AUDIO_ENABLE_SND_DEVICE: device %d already open\n", dev_id);
		MM_DBG("AUDIO_ENABLE_SND_DEVICE %d: exit %d\n", dev_id, rc);
		break;

	}

	case AUDIO_DISABLE_SND_DEVICE: {
		struct msm_snddev_info *dev_info;
		u32 dev_id;

		MM_DBG("AUDIO_DISABLE_SND_DEVICE: entry\n");
		if (get_user(dev_id, (u32 __user *) arg)) {
			rc = -EFAULT;
			break;
		}
		dev_info = audio_dev_ctrl_find_dev(dev_id);
		if (IS_ERR(dev_info))
			rc = PTR_ERR(dev_info);
		else if (dev_info->opened) {
#if 0 //def REC_TEST
			mutex_unlock(&session_lock);
	        	broadcast_event(AUDDEV_EVT_REL_PENDING, dev_id, SESSION_IGNORE);
			mutex_lock(&session_lock);
#endif
			rc = dev_info->dev_ops.close(dev_info);
			if(rc < 0) { 
				MM_ERR("error disabling device!");
			//	break;
		}
			dev_info->opened = 0;
#if 0 //def REC_TEST
			mutex_unlock(&session_lock);
			broadcast_event(AUDDEV_EVT_DEV_RLS, dev_id, SESSION_IGNORE);
			mutex_lock(&session_lock);
#endif
		} else MM_DBG("AUDIO_DISABLE_SND_DEVICE: device %d already closed\n", dev_id);
		MM_DBG("AUDIO_DISABLE_SND_DEVICE: done, dev=%d (%s)\n", dev_id, dev_info->name);
		break;
	}

	case AUDIO_ROUTE_STREAM: {
		struct msm_audio_route_config route_cfg;
		struct msm_snddev_info *dev_info;
#ifdef REC_TEST
		uint32_t session_mask;
		uint32_t session_id, set;
#endif
		if (copy_from_user(&route_cfg, (void __user *) arg, sizeof(struct msm_audio_route_config))) {
			rc = -EFAULT;
			break;
		}

		MM_DBG("AUDIO_ROUTE_STREAM: dev_id=%d type=%d stream_id=%d\n", 
				route_cfg.dev_id, route_cfg.stream_type, route_cfg.stream_id & ~0x80000000);

		dev_info = audio_dev_ctrl_find_dev(route_cfg.dev_id);

		if (IS_ERR(dev_info)) {
			MM_AUD_ERR("%s: pass invalid dev_id\n", __func__);
			rc = PTR_ERR(dev_info);
			break;
		}

		switch (route_cfg.stream_type) {

		case AUDIO_ROUTE_STREAM_VOICE_RX:

			if (!(dev_info->capability & SNDDEV_CAP_RX) |
			    !(dev_info->capability & SNDDEV_CAP_VOICE)) {
				rc = -EINVAL;
				break;
			}
			dev_ctrl->voice_rx_dev = dev_info;
			break;
#if 0
			if (!(dev_info->capability & SNDDEV_CAP_RX) |
			    !(dev_info->capability & SNDDEV_CAP_VOICE)) {
				rc = -EINVAL;
				break;
			}
			set = (route_cfg.stream_id & 0x80000000);
			if (dev_info->acdb_id != 10 && dev_info->acdb_id < 1000) {
				struct snddev_icodec_state *icodec = (struct snddev_icodec_state *)dev_info->private_data;
				struct adie_codec_hwsetting_entry *entry = icodec->data->profile->settings;
				int i, j = icodec->data->profile->setting_sz;
				set = (route_cfg.stream_id & 0x80000000);
				if (set) {
					for (i = 0; i < j; i++)
						if (entry[i].voc_action != NULL) {
							entry[i].actions = entry[i].voc_action;
							entry[i].action_sz = entry[i].voc_action_sz;
						}
				} else {
					for (i = 0; i < j; i++)
						if (entry[i].midi_action != NULL) {
							entry[i].actions = entry[i].midi_action;
							entry[i].action_sz = entry[i].midi_action_sz;
						}
				}
			}
			mutex_unlock(&session_lock);
			rc = msm_set_voc_route(dev_info,AUDIO_ROUTE_STREAM_VOICE_RX,route_cfg.dev_id);
			if(rc == 0 && set && dev_info->opened)
				broadcast_event(AUDDEV_EVT_DEV_RDY, route_cfg.dev_id,
                                	0x1 << (8 * ((int)AUDDEV_CLNT_VOC-1)));
			MM_DBG("AUDIO_ROUTE_STREAM: exit %d\n", rc);
			return rc;

#endif
		case AUDIO_ROUTE_STREAM_VOICE_TX:

			if (!(dev_info->capability & SNDDEV_CAP_TX) |
			    !(dev_info->capability & SNDDEV_CAP_VOICE)) {
				rc = -EINVAL;
				break;
			}
			dev_ctrl->voice_tx_dev = dev_info;
#if 0 //REC_TEST
			session_id = (route_cfg.stream_id & 0xFFFF);
			set = (route_cfg.stream_id & 0x80000000) ? 1 : 0;
			session_mask = (0x1 << (session_id)) << (8 * ((int)AUDDEV_CLNT_ENC-1));
			rc = msm_snddev_set_enc(session_id, dev_info->copp_id, set);
#if 0 
			if(!set) {
				dev_info->sessions &= ~(session_mask);
				mutex_unlock(&session_lock);
				if (dev_info->opened) broadcast_event(AUDDEV_EVT_DEV_RLS, route_cfg.dev_id, session_mask);
			} else {
				dev_info->sessions = dev_info->sessions | session_mask;
				mutex_unlock(&session_lock);
				if (dev_info->opened) broadcast_event(AUDDEV_EVT_DEV_RDY, route_cfg.dev_id, session_mask);
			}
#endif
                        if (dev_info->acdb_id != 9 && dev_info->acdb_id < 1000) {
                                struct snddev_icodec_state *icodec = (struct snddev_icodec_state *)dev_info->private_data;
                                struct adie_codec_hwsetting_entry *entry = icodec->data->profile->settings;
                                int i, j = icodec->data->profile->setting_sz;
                                if (set) {
                                        for (i = 0; i < j; i++)
                                                if (entry[i].voc_action != NULL) {
                                                        entry[i].actions = entry[i].voc_action;
                                                        entry[i].action_sz = entry[i].voc_action_sz;
                                                }
                                } else {
                                        for (i = 0; i < j; i++)
                                                if (entry[i].midi_action != NULL) {
                                                        entry[i].actions = entry[i].midi_action;
                                                        entry[i].action_sz = entry[i].midi_action_sz;
                                                }
                                }
                        }
			mutex_unlock(&session_lock);
			msm_set_voice_mute(DIR_TX, !set);
			rc = msm_set_voc_route(dev_info,AUDIO_ROUTE_STREAM_VOICE_TX,route_cfg.dev_id);
#if 0
			if(rc == 0) {
				broadcast_event(AUDDEV_EVT_DEV_CHG_VOICE, route_cfg.dev_id, 
					0x1 << (8 * ((int)AUDDEV_CLNT_VOC-1)));
				if(set && dev_info->opened) 
					broadcast_event(AUDDEV_EVT_DEV_RDY, route_cfg.dev_id, 
						0x1 << (8 * ((int)AUDDEV_CLNT_VOC-1)));
			}
#endif
			MM_DBG("AUDIO_ROUTE_STREAM: exit %d\n", rc);
			return rc;

#endif
			break;

//////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef REC_TEST
		    case AUDIO_ROUTE_STREAM_REC: {

		        int enc_freq = 0;
		        int requested_freq = 0;

			MM_DBG("AUDIO_ROUTE_STREAM_REC\n");

			session_id = (route_cfg.stream_id & ~0x80000000);
			set = ((route_cfg.stream_id & 0x80000000) == 0) ? 0 : 1;
			session_mask = (0x1 << (session_id)) << (8 * ((int)AUDDEV_CLNT_ENC-1));
			rc = msm_snddev_set_enc(session_id, dev_info->copp_id, set);
			if (!set) {
				MM_DBG("not set, rc = %d\n",rc);
				dev_info->sessions &= ~(session_mask);
				mutex_unlock(&session_lock);
				if (dev_info->opened) broadcast_event(AUDDEV_EVT_DEV_RLS, route_cfg.dev_id, session_mask);
				return rc;
			} else {
				MM_DBG("encoder set, rc = %d\n",rc);
				dev_info->sessions = dev_info->sessions | session_mask;
				enc_freq = msm_snddev_get_enc_freq(session_id);
				requested_freq = enc_freq;
				if (enc_freq > 0) {
					rc = msm_snddev_request_freq(&enc_freq, session_id, SNDDEV_CAP_TX, AUDDEV_CLNT_ENC);
					MM_DBG("sample rate configured %d sample rate requested %d \n", enc_freq, requested_freq);
	                                if ((rc <= 0) || (enc_freq != requested_freq)) {
        	                                MM_DBG("failure, msm_snddev_withdraw_freq!\n");
                	                        rc = msm_snddev_withdraw_freq(session_id, SNDDEV_CAP_TX, AUDDEV_CLNT_ENC);
                        	                broadcast_event(AUDDEV_EVT_FREQ_CHG, route_cfg.dev_id, SESSION_IGNORE);
                                	}
	                        }
				MM_DBG("route set\n");
				mutex_unlock(&session_lock);
				if (dev_info->opened) broadcast_event(AUDDEV_EVT_DEV_RDY, route_cfg.dev_id, session_mask);
				return rc;
		}
		break;
	}
#endif
		    /* case AUDIO_ROUTE_STREAM_PLAYBACK: */
		    default:
			MM_ERR("They forgot to write me!!!!!\n");			
			break;
		} /* end switch(route_cfg.stream_type) */
		MM_DBG("AUDIO_ROUTE_STREAM: exit %d\n", rc);	
		break;
	} /* end case AUDIO_ROUTE_STREAM */

#ifdef REC_TEST
#define AUDIO_GET_ROUTING_INFO  _IOR(AUDIO_IOCTL_MAGIC, 57, struct audio_routing_info)
#define AUDIO_GET_ROUTING_DATA  _IOR(AUDIO_IOCTL_MAGIC, 58, struct rt_data)

	case AUDIO_GET_ROUTING_INFO: {
		if (copy_to_user((void *) arg, &routing_info, sizeof(routing_info))) rc = -EFAULT;
		break;
	}

	case AUDIO_GET_ROUTING_DATA: {
		uint32_t k;
		struct rt_data rtd;

		memset(&rtd, 0, sizeof rtd);
		for (k = 0; k < 32; k++)
			if (audio_dev_ctrl.devs[k] && audio_dev_ctrl.devs[k]->opened) rtd.enabled_devs |= (1 << k);
		if (audio_dev_ctrl.voice_rx_dev) {
			rtd.rx_valid = 1; memcpy(&rtd.voice_rx_dev, audio_dev_ctrl.voice_rx_dev, sizeof(struct msm_snddev_info));
		}
		if (audio_dev_ctrl.voice_tx_dev) {
			rtd.tx_valid = 1; memcpy(&rtd.voice_tx_dev, audio_dev_ctrl.voice_tx_dev, sizeof(struct msm_snddev_info));
		}
		if (copy_to_user((void *) arg, &rtd, sizeof rtd)) rc = -EFAULT;
		break;
	}
#endif
	default:
		rc = -EINVAL;
	} /* end switch (cmd) */
	mutex_unlock(&session_lock);
	return rc;
}

static int audio_dev_ctrl_open(struct inode *inode, struct file *file)
{
	MM_DBG("open audio_dev_ctrl\n");
	atomic_inc(&audio_dev_ctrl.opened);
	file->private_data = &audio_dev_ctrl;
	return 0;
}

static int audio_dev_ctrl_release(struct inode *inode, struct file *file)
{
	MM_DBG("release audio_dev_ctrl\n");
	atomic_dec(&audio_dev_ctrl.opened);
	return 0;
}

static const struct file_operations audio_dev_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = audio_dev_ctrl_open,
	.release = audio_dev_ctrl_release,
	.ioctl = audio_dev_ctrl_ioctl,
};


struct miscdevice audio_dev_ctrl_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "msm_audio_dev_ctrl",
	.fops	= &audio_dev_ctrl_fops,
};

/* session id is 32 bit routing mask per device
 * 0-7 for voice clients
 * 8-15 for Decoder clients
 * 16-23 for Encoder clients
 * 24-31 Do not care
 */
void broadcast_event(u32 evt_id, u32 dev_id, u32 session_id)
{
	int clnt_id = 0;
	union auddev_evt_data *evt_payload;
	struct msm_snd_evt_listner *callback;
	struct msm_snddev_info *dev_info = NULL;
	u32 session_mask = 0;
	static int pending_sent;

	MM_DBG(": evt_id = %d, dev_id = %d\n", evt_id, dev_id);

	if ((evt_id != AUDDEV_EVT_START_VOICE)
		&& (evt_id != AUDDEV_EVT_END_VOICE)
                && (evt_id != AUDDEV_EVT_STREAM_VOL_CHG)
                && (evt_id != AUDDEV_EVT_VOICE_STATE_CHG)) {
		dev_info = audio_dev_ctrl_find_dev(dev_id);
                if (IS_ERR(dev_info)) {
                        MM_ERR("pass invalid dev_id\n");
			return;
	}
        }

	if(event.cb == NULL) {
		if (evt_id == AUDDEV_EVT_VOICE_STATE_CHG) {
			mutex_lock(&session_lock);
			MM_AUD_ERR("Got AUDDEV_EVT_VOICE_STATE_CHG %d but nobody cares!\n", dev_id);
                	routing_info.voice_state = dev_id;
			mutex_unlock(&session_lock);
		}
		return;
	}

	callback = event.cb;

	mutex_lock(&session_lock);

        if (evt_id == AUDDEV_EVT_VOICE_STATE_CHG)
                routing_info.voice_state = dev_id;

	evt_payload = kzalloc(sizeof(union auddev_evt_data),
			GFP_KERNEL);

	if (!evt_payload) {
		MM_AUD_ERR("%s: fail to allocate memory\n", __func__);
		return;
	}

	for (; ;) {
		if (!(evt_id & callback->evt_id)) {
			if (callback->cb_next == NULL)
				break;
			else {
				callback = callback->cb_next;
				continue;
			}
		}
		clnt_id = callback->clnt_id;
		memset(evt_payload, 0, sizeof(union auddev_evt_data));

		if ((evt_id == AUDDEV_EVT_START_VOICE)
			|| (evt_id == AUDDEV_EVT_END_VOICE))
			goto skip_check;
		if (callback->clnt_type == AUDDEV_CLNT_AUDIOCAL)
			goto aud_cal;

		session_mask = (0x1 << (clnt_id))
				<< (8 * ((int)callback->clnt_type-1));

                if ((evt_id == AUDDEV_EVT_STREAM_VOL_CHG) || 
                        (evt_id == AUDDEV_EVT_VOICE_STATE_CHG)) {
			MM_DBG("AUDDEV_EVT_STREAM_VOL_CHG or AUDDEV_EVT_VOICE_STATE_CHG\n");
			goto volume_strm;
		}

		MM_DBG("dev_info->sessions = %08x\n", dev_info->sessions);

		if ((!session_id && !(dev_info->sessions & session_mask)) ||
			(session_id && ((dev_info->sessions & session_mask) !=
						session_id))) {
			if (callback->cb_next == NULL)
				break;
			else {
				callback = callback->cb_next;
				continue;
			}
		}
		if (evt_id == AUDDEV_EVT_DEV_CHG_VOICE)
			goto voc_events;

volume_strm:
		if (callback->clnt_type == AUDDEV_CLNT_DEC) {
			MM_DBG("AUDDEV_CLNT_DEC\n");
			if (evt_id == AUDDEV_EVT_STREAM_VOL_CHG) {
				MM_DBG("clnt_id = %d, session_id = 0x%8x\n",
					clnt_id, session_id);
				if (session_mask != session_id)
					goto sent_dec;
				else
					evt_payload->session_vol =
						msm_vol_ctl.volume;
			} else if (evt_id == AUDDEV_EVT_FREQ_CHG) {
				if (routing_info.dec_freq[clnt_id].evt) {
					routing_info.dec_freq[clnt_id].evt
							= 0;
					goto sent_dec;
				} else if (routing_info.dec_freq[clnt_id].freq
					== dev_info->set_sample_rate)
					goto sent_dec;
				else {
					evt_payload->freq_info.sample_rate
						= dev_info->set_sample_rate;
					evt_payload->freq_info.dev_type
						= dev_info->capability;
					evt_payload->freq_info.acdb_dev_id
						= dev_info->acdb_id;
				}
			} else if (evt_id == AUDDEV_EVT_VOICE_STATE_CHG) {
                                evt_payload->voice_state =
                                        routing_info.voice_state;
			}  else if (dev_info != NULL) {
				evt_payload->routing_id = dev_info->copp_id;
			}
			callback->auddev_evt_listener(
					evt_id,
					evt_payload,
					callback->private_data);
sent_dec:
                        if ((evt_id != AUDDEV_EVT_STREAM_VOL_CHG) &&
                                (evt_id != AUDDEV_EVT_VOICE_STATE_CHG))
				routing_info.dec_freq[clnt_id].freq
						= dev_info->set_sample_rate;

			if (callback->cb_next == NULL)
				break;
			else {
				callback = callback->cb_next;
				continue;
			}
		}
		if (callback->clnt_type == AUDDEV_CLNT_ENC) {
			MM_DBG("AUDDEV_CLNT_ENC\n");
			if (evt_id == AUDDEV_EVT_FREQ_CHG) {
				if (routing_info.enc_freq[clnt_id].evt) {
					routing_info.enc_freq[clnt_id].evt
							= 0;
					goto sent_enc;
				 } else {
					evt_payload->freq_info.sample_rate
						= dev_info->set_sample_rate;
					evt_payload->freq_info.dev_type
						= dev_info->capability;
					evt_payload->freq_info.acdb_dev_id
						= dev_info->acdb_id;
				}
			} else if (evt_id == AUDDEV_EVT_VOICE_STATE_CHG) {
                                evt_payload->voice_state =
                                        routing_info.voice_state;
   			} else if (dev_info != NULL) {
				evt_payload->routing_id = dev_info->copp_id;
			}
			callback->auddev_evt_listener(
					evt_id,
					evt_payload,
					callback->private_data);
sent_enc:
			if (callback->cb_next == NULL)
					break;
			else {
				callback = callback->cb_next;
				continue;
			}
		}
aud_cal:
		if (callback->clnt_type == AUDDEV_CLNT_AUDIOCAL) {
                        int temp_sessions;
			MM_DBG("AUDDEV_CLNT_AUDIOCAL\n");
			if (evt_id == AUDDEV_EVT_VOICE_STATE_CHG)
                                evt_payload->voice_state =
                                        routing_info.voice_state;
                        else if (!dev_info->sessions)
				goto sent_aud_cal;
			else {
                                evt_payload->audcal_info.dev_id =
                                                dev_info->copp_id;
			evt_payload->audcal_info.acdb_id =
				dev_info->acdb_id;
			evt_payload->audcal_info.dev_type =
				(dev_info->capability & SNDDEV_CAP_TX) ?
				SNDDEV_CAP_TX : SNDDEV_CAP_RX;
			evt_payload->audcal_info.sample_rate =
				dev_info->set_sample_rate ?
				dev_info->set_sample_rate :
				dev_info->sample_rate;
                        }
                        if (evt_payload->audcal_info.dev_type ==
                                                SNDDEV_CAP_TX) {
                                if (session_id == SESSION_IGNORE)
                                        temp_sessions = dev_info->sessions;
                                else
                                        temp_sessions = session_id;
                                evt_payload->audcal_info.sessions =
                                        (temp_sessions >>
                                                ((AUDDEV_CLNT_ENC-1) * 8));
                        } else {
                                if (session_id == SESSION_IGNORE)
                                        temp_sessions = dev_info->sessions;
                                else
                                        temp_sessions = session_id;
                                evt_payload->audcal_info.sessions =
                                        (temp_sessions >>
                                                ((AUDDEV_CLNT_DEC-1) * 8));
                        }
			callback->auddev_evt_listener(
				evt_id,
				evt_payload,
				callback->private_data);

sent_aud_cal:
			if (callback->cb_next == NULL)
				break;
			else {
				callback = callback->cb_next;
				continue;
			}
		}
skip_check:
voc_events:
		if (callback->clnt_type == AUDDEV_CLNT_VOC) {
			MM_DBG("AUDDEV_CLNT_VOC\n");
			if (evt_id == AUDDEV_EVT_DEV_RLS) {
				if (!pending_sent)
					goto sent_voc;
				else
					pending_sent = 0;
			}
			if (evt_id == AUDDEV_EVT_REL_PENDING)
				pending_sent = 1;

			if (evt_id == AUDDEV_EVT_DEVICE_VOL_MUTE_CHG) {
				if (dev_info->capability & SNDDEV_CAP_TX) {
					evt_payload->voc_vm_info.dev_type =
						SNDDEV_CAP_TX;
					evt_payload->voc_vm_info.acdb_dev_id =
						dev_info->acdb_id;
					evt_payload->
					voc_vm_info.dev_vm_val.mute =
						routing_info.tx_mute;
				} else {
					evt_payload->voc_vm_info.dev_type =
						SNDDEV_CAP_RX;
					evt_payload->voc_vm_info.acdb_dev_id =
						dev_info->acdb_id;
					if (routing_info.voice_rx_vol < 0)
						evt_payload->
						voc_vm_info.dev_vm_val.mute =
							routing_info.rx_mute;
					else
						evt_payload->
						voc_vm_info.dev_vm_val.vol =
							routing_info.voice_rx_vol;
				}
			} else if ((evt_id == AUDDEV_EVT_START_VOICE)
					|| (evt_id == AUDDEV_EVT_END_VOICE)) {
				memset(evt_payload, 0,
					sizeof(union auddev_evt_data));
			} else if (evt_id == AUDDEV_EVT_FREQ_CHG) {
				if (routing_info.voice_tx_sample_rate
						!= dev_info->set_sample_rate) {
					routing_info.voice_tx_sample_rate
						= dev_info->set_sample_rate;
					evt_payload->freq_info.sample_rate
						= dev_info->set_sample_rate;
					evt_payload->freq_info.dev_type
						= dev_info->capability;
					evt_payload->freq_info.acdb_dev_id
						= dev_info->acdb_id;
				} else
					goto sent_voc;
			} else if (evt_id == AUDDEV_EVT_VOICE_STATE_CHG) {
                                evt_payload->voice_state =
                                                routing_info.voice_state;
			} else if (dev_info != NULL) {
				evt_payload->voc_devinfo.dev_type =
					(dev_info->capability & SNDDEV_CAP_TX) ?
					SNDDEV_CAP_TX : SNDDEV_CAP_RX;
				evt_payload->voc_devinfo.acdb_dev_id =
					dev_info->acdb_id;
				evt_payload->voc_devinfo.dev_sample =
					dev_info->set_sample_rate ?
					dev_info->set_sample_rate :
					dev_info->sample_rate;
				evt_payload->voc_devinfo.dev_id = dev_id;
				if (dev_info->capability & SNDDEV_CAP_RX)
					evt_payload->voc_devinfo.vol_idx = dev_info->vol_idx;
			}
			callback->auddev_evt_listener(
				evt_id,
				evt_payload,
				callback->private_data);
			if (evt_id == AUDDEV_EVT_DEV_RLS)
				dev_info->sessions &= ~(0xFF);
sent_voc:
			if (callback->cb_next == NULL)
				break;
			else {
				callback = callback->cb_next;
				continue;
			}
		}
	}
	kfree(evt_payload);
	mutex_unlock(&session_lock);
}
EXPORT_SYMBOL(broadcast_event);


void mixer_post_event(u32 evt_id, u32 id)
{

	MM_DBG("evt_id = %d\n", evt_id);
	switch (evt_id) {
	case AUDDEV_EVT_DEV_CHG_VOICE: /* Called from Voice_route */
		broadcast_event(AUDDEV_EVT_DEV_CHG_VOICE, id, SESSION_IGNORE);
		break;
	case AUDDEV_EVT_DEV_RDY:
		broadcast_event(AUDDEV_EVT_DEV_RDY, id, SESSION_IGNORE);
		break;
	case AUDDEV_EVT_DEV_RLS:
		broadcast_event(AUDDEV_EVT_DEV_RLS, id, SESSION_IGNORE);
		break;
	case AUDDEV_EVT_REL_PENDING:
		broadcast_event(AUDDEV_EVT_REL_PENDING, id, SESSION_IGNORE);
		break;
	case AUDDEV_EVT_DEVICE_VOL_MUTE_CHG:
		broadcast_event(AUDDEV_EVT_DEVICE_VOL_MUTE_CHG, id,
							SESSION_IGNORE);
		break;
	case AUDDEV_EVT_STREAM_VOL_CHG:
		broadcast_event(AUDDEV_EVT_STREAM_VOL_CHG, id,
							SESSION_IGNORE);
		break;
	case AUDDEV_EVT_START_VOICE:
		broadcast_event(AUDDEV_EVT_START_VOICE,
				id, SESSION_IGNORE);
		break;
	case AUDDEV_EVT_END_VOICE:
		broadcast_event(AUDDEV_EVT_END_VOICE,
				id, SESSION_IGNORE);
		break;
	case AUDDEV_EVT_FREQ_CHG:
		broadcast_event(AUDDEV_EVT_FREQ_CHG, id, SESSION_IGNORE);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(mixer_post_event);

static int __init audio_dev_ctrl_init(void)
{
//	init_waitqueue_head(&audio_dev_ctrl.wait);

	event.cb = NULL;
	atomic_set(&audio_dev_ctrl.opened, 0);
	audio_dev_ctrl.num_dev = 0;
	audio_dev_ctrl.voice_tx_dev = NULL;
	audio_dev_ctrl.voice_rx_dev = NULL;
        routing_info.voice_state = VOICE_STATE_INVALID;
	return misc_register(&audio_dev_ctrl_misc);
}

static void __exit audio_dev_ctrl_exit(void)
{
}
module_init(audio_dev_ctrl_init);
module_exit(audio_dev_ctrl_exit);

MODULE_DESCRIPTION("MSM 7K Audio Device Control driver");
MODULE_LICENSE("Dual BSD/GPL");
