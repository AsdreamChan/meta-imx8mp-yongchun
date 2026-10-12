// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test pattern generator: a V4L2 capture device per yongchun,tpg device
 * tree node, producing frames at the rate the node describes.
 *
 * The rate is a fraction of a second per frame, numerator first, the
 * same shape as V4L2's timeperframe: <1 30> is 30 frames per second,
 * <1 9> is 9. A fraction also expresses a period that is not a whole
 * number of microseconds.
 *
 * Frames are paced by an hrtimer, not by jiffies: with HZ=250 a jiffy
 * timer cannot hit 33.3 ms and alternates 32 and 36 ms (vivid does). The
 * timer callback runs in softirq context, so it only takes the next
 * queued buffer, stamps it and moves it to a second list; drawing the
 * pattern, which touches the whole frame, is left to a work item.
 */

#include <linux/hrtimer.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/workqueue.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

struct yc_tpg_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct yc_tpg {
	struct device *dev;
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct vb2_queue queue;
	struct mutex lock;		/* serialises ioctls and the queue */

	u32 interval[2];		/* seconds per frame: num, den */
	u64 period_ns;
	struct v4l2_pix_format fmt;

	spinlock_t qlock;		/* protects the lists and sequence */
	struct list_head buf_list;	/* queued, waiting for a tick */
	struct list_head done_list;	/* stamped, waiting to be drawn */
	u32 sequence;

	struct hrtimer timer;
	struct work_struct fill_work;
};

static const struct v4l2_frmsize_discrete yc_tpg_sizes[] = {
	{ 320, 240 },
	{ 640, 480 },
	{ 1280, 720 },
};

/* 75% colour bars, BT.601 limited range: Y, Cb, Cr */
static const u8 yc_tpg_bars[8][3] = {
	{ 180, 128, 128 },	/* white */
	{ 162,  44, 142 },	/* yellow */
	{ 131, 156,  44 },	/* cyan */
	{ 112,  72,  58 },	/* green */
	{  84, 184, 198 },	/* magenta */
	{  65, 100, 212 },	/* red */
	{  35, 212, 114 },	/* blue */
	{  16, 128, 128 },	/* black */
};

static inline struct yc_tpg_buffer *to_yc_tpg_buffer(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct yc_tpg_buffer, vb);
}

static void yc_tpg_fill_pix_format(struct v4l2_pix_format *pix)
{
	const struct v4l2_frmsize_discrete *size;

	size = v4l2_find_nearest_size(yc_tpg_sizes, ARRAY_SIZE(yc_tpg_sizes),
				      width, height, pix->width, pix->height);

	pix->width = size->width;
	pix->height = size->height;
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

/*
 * Draw the bars into one line, shifted a little each frame so that a
 * still image is easy to tell from a stalled stream, then copy that line
 * down the frame.
 */
static void yc_tpg_draw(struct yc_tpg *tpg, struct yc_tpg_buffer *buf)
{
	u8 *p = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	u32 width = tpg->fmt.width;
	u32 bpl = tpg->fmt.bytesperline;
	u32 shift = (buf->vb.sequence * 4) % width;
	u32 x, y;

	if (!p)
		return;

	/* YUYV packs two pixels in four bytes, Y0 Cb Y1 Cr, sharing Cb/Cr */
	for (x = 0; x < width; x += 2) {
		const u8 *c = yc_tpg_bars[((x + shift) * 8 / width) % 8];

		p[x * 2 + 0] = c[0];
		p[x * 2 + 1] = c[1];
		p[x * 2 + 2] = c[0];
		p[x * 2 + 3] = c[2];
	}

	for (y = 1; y < tpg->fmt.height; y++)
		memcpy(p + y * bpl, p, bpl);
}

static void yc_tpg_fill_work(struct work_struct *work)
{
	struct yc_tpg *tpg = container_of(work, struct yc_tpg, fill_work);
	struct yc_tpg_buffer *buf;
	unsigned long flags;

	for (;;) {
		spin_lock_irqsave(&tpg->qlock, flags);
		buf = list_first_entry_or_null(&tpg->done_list,
					       struct yc_tpg_buffer, list);
		if (buf)
			list_del(&buf->list);
		spin_unlock_irqrestore(&tpg->qlock, flags);

		if (!buf)
			break;

		yc_tpg_draw(tpg, buf);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
}

/*
 * One tick is one frame period. hrtimer_forward_now() returns how many
 * periods have passed, normally 1; if the callback ran late, the frames
 * for the periods that were skipped are counted as dropped, so the
 * sequence number shows the gap. A tick that finds no queued buffer is
 * a dropped frame too.
 *
 * The timestamp is the time the callback runs, not the time the timer
 * was due, so it includes how long the CPU took to wake up for it.
 */
static enum hrtimer_restart yc_tpg_tick(struct hrtimer *timer)
{
	struct yc_tpg *tpg = container_of(timer, struct yc_tpg, timer);
	u64 now = ktime_get_ns();
	u64 periods = hrtimer_forward_now(timer, ns_to_ktime(tpg->period_ns));
	struct yc_tpg_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&tpg->qlock, flags);
	buf = list_first_entry_or_null(&tpg->buf_list,
				       struct yc_tpg_buffer, list);
	if (buf) {
		buf->vb.vb2_buf.timestamp = now;
		buf->vb.sequence = tpg->sequence;
		buf->vb.field = V4L2_FIELD_NONE;
		list_move_tail(&buf->list, &tpg->done_list);
	}
	tpg->sequence += periods;
	spin_unlock_irqrestore(&tpg->qlock, flags);

	if (buf)
		queue_work(system_highpri_wq, &tpg->fill_work);
	else
		dev_dbg(tpg->dev, "no buffer queued, frame dropped\n");

	if (periods > 1)
		dev_dbg(tpg->dev, "tick late, %llu periods passed\n", periods);

	return HRTIMER_RESTART;
}

/* vb2 queue operations */

static int yc_tpg_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			      unsigned int *nplanes, unsigned int sizes[],
			      struct device *alloc_devs[])
{
	struct yc_tpg *tpg = vb2_get_drv_priv(vq);

	if (*nplanes)
		return sizes[0] < tpg->fmt.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = tpg->fmt.sizeimage;
	return 0;
}

static int yc_tpg_buf_prepare(struct vb2_buffer *vb)
{
	struct yc_tpg *tpg = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = tpg->fmt.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(tpg->dev, "buffer too small (%lu < %lu)\n",
			vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

static void yc_tpg_buf_queue(struct vb2_buffer *vb)
{
	struct yc_tpg *tpg = vb2_get_drv_priv(vb->vb2_queue);
	struct yc_tpg_buffer *buf = to_yc_tpg_buffer(to_vb2_v4l2_buffer(vb));
	unsigned long flags;

	spin_lock_irqsave(&tpg->qlock, flags);
	list_add_tail(&buf->list, &tpg->buf_list);
	spin_unlock_irqrestore(&tpg->qlock, flags);

	dev_dbg(tpg->dev, "queued buffer %u\n", vb->index);
}

static void yc_tpg_return_all(struct yc_tpg *tpg, enum vb2_buffer_state state)
{
	struct yc_tpg_buffer *buf, *next;
	unsigned long flags;

	spin_lock_irqsave(&tpg->qlock, flags);
	list_for_each_entry_safe(buf, next, &tpg->done_list, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	list_for_each_entry_safe(buf, next, &tpg->buf_list, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&tpg->qlock, flags);
}

static int yc_tpg_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct yc_tpg *tpg = vb2_get_drv_priv(vq);

	tpg->sequence = 0;
	hrtimer_start(&tpg->timer, ns_to_ktime(tpg->period_ns),
		      HRTIMER_MODE_REL_SOFT);

	dev_dbg(tpg->dev, "streaming started, %u buffers queued\n", count);
	return 0;
}

static void yc_tpg_stop_streaming(struct vb2_queue *vq)
{
	struct yc_tpg *tpg = vb2_get_drv_priv(vq);

	/*
	 * Timer first: a tick can still queue the work. Once both are
	 * stopped nothing else touches the lists, and every buffer the
	 * driver holds goes back to vb2.
	 */
	hrtimer_cancel(&tpg->timer);
	cancel_work_sync(&tpg->fill_work);
	yc_tpg_return_all(tpg, VB2_BUF_STATE_ERROR);

	dev_dbg(tpg->dev, "streaming stopped at sequence %u\n", tpg->sequence);
}

static const struct vb2_ops yc_tpg_qops = {
	.queue_setup		= yc_tpg_queue_setup,
	.buf_prepare		= yc_tpg_buf_prepare,
	.buf_queue		= yc_tpg_buf_queue,
	.start_streaming	= yc_tpg_start_streaming,
	.stop_streaming		= yc_tpg_stop_streaming,
};

/* V4L2 ioctls */

static int yc_tpg_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct yc_tpg *tpg = video_drvdata(file);

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	snprintf(cap->card, sizeof(cap->card), "yongchun-tpg %s",
		 dev_name(tpg->dev));
	/* bus_info is filled in by the core from the parent device */
	return 0;
}

static int yc_tpg_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_YUYV;
	return 0;
}

static int yc_tpg_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct yc_tpg *tpg = video_drvdata(file);

	f->fmt.pix = tpg->fmt;
	return 0;
}

static int yc_tpg_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	yc_tpg_fill_pix_format(&f->fmt.pix);
	return 0;
}

static int yc_tpg_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct yc_tpg *tpg = video_drvdata(file);

	if (vb2_is_busy(&tpg->queue))
		return -EBUSY;

	yc_tpg_fill_pix_format(&f->fmt.pix);
	tpg->fmt = f->fmt.pix;
	return 0;
}

static int yc_tpg_enum_framesizes(struct file *file, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	if (fsize->pixel_format != V4L2_PIX_FMT_YUYV ||
	    fsize->index >= ARRAY_SIZE(yc_tpg_sizes))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete = yc_tpg_sizes[fsize->index];
	return 0;
}

static int yc_tpg_enum_frameintervals(struct file *file, void *priv,
				      struct v4l2_frmivalenum *fival)
{
	struct yc_tpg *tpg = video_drvdata(file);
	unsigned int i;

	if (fival->index || fival->pixel_format != V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(yc_tpg_sizes); i++)
		if (fival->width == yc_tpg_sizes[i].width &&
		    fival->height == yc_tpg_sizes[i].height)
			break;
	if (i == ARRAY_SIZE(yc_tpg_sizes))
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = tpg->interval[0];
	fival->discrete.denominator = tpg->interval[1];
	return 0;
}

static int yc_tpg_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	struct yc_tpg *tpg = video_drvdata(file);

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/*
	 * Mainline v4l_s_parm() clears reserved[], extendedmode and
	 * capturemode before calling the driver; linux-imx's does not, so
	 * S_PARM would hand back whatever the caller left there. Start
	 * from a cleared structure on both paths.
	 */
	memset(&parm->parm.capture, 0, sizeof(parm->parm.capture));
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe.numerator = tpg->interval[0];
	parm->parm.capture.timeperframe.denominator = tpg->interval[1];
	return 0;
}

/*
 * The rate comes from the device tree and is fixed. A request for any
 * other rate is answered with the one the device has, which is how V4L2
 * negotiation works: the driver replies with what it can do.
 */
static int yc_tpg_s_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	return yc_tpg_g_parm(file, priv, parm);
}

static int yc_tpg_enum_input(struct file *file, void *priv,
			     struct v4l2_input *inp)
{
	if (inp->index)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, "Test pattern", sizeof(inp->name));
	return 0;
}

static int yc_tpg_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int yc_tpg_s_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static const struct v4l2_ioctl_ops yc_tpg_ioctl_ops = {
	.vidioc_querycap		= yc_tpg_querycap,

	.vidioc_enum_fmt_vid_cap	= yc_tpg_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= yc_tpg_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= yc_tpg_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= yc_tpg_s_fmt_vid_cap,
	.vidioc_enum_framesizes		= yc_tpg_enum_framesizes,
	.vidioc_enum_frameintervals	= yc_tpg_enum_frameintervals,
	.vidioc_g_parm			= yc_tpg_g_parm,
	.vidioc_s_parm			= yc_tpg_s_parm,

	.vidioc_enum_input		= yc_tpg_enum_input,
	.vidioc_g_input			= yc_tpg_g_input,
	.vidioc_s_input			= yc_tpg_s_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations yc_tpg_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll,
};

/*
 * The device can be unbound while a program still holds /dev/videoN
 * open, so the structure cannot be freed in remove(). It lives until
 * the last reference to the v4l2_device is dropped, and is freed here.
 */
static void yc_tpg_release(struct v4l2_device *v4l2_dev)
{
	struct yc_tpg *tpg = container_of(v4l2_dev, struct yc_tpg, v4l2_dev);

	v4l2_device_unregister(&tpg->v4l2_dev);
	mutex_destroy(&tpg->lock);
	kfree(tpg);
}

static int yc_tpg_read_interval(struct device *dev, struct yc_tpg *tpg)
{
	int ret;

	ret = device_property_read_u32_array(dev, "yongchun,frame-interval",
					     tpg->interval,
					     ARRAY_SIZE(tpg->interval));
	if (ret)
		return dev_err_probe(dev, ret,
				     "cannot read yongchun,frame-interval\n");

	if (!tpg->interval[0] || !tpg->interval[1])
		return dev_err_probe(dev, -EINVAL,
				     "frame interval %u/%u is not valid\n",
				     tpg->interval[0], tpg->interval[1]);

	tpg->period_ns = div_u64((u64)tpg->interval[0] * NSEC_PER_SEC,
				 tpg->interval[1]);
	return 0;
}

static int yc_tpg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct video_device *vdev;
	struct vb2_queue *q;
	struct yc_tpg *tpg;
	int ret;

	dev_dbg(dev, "probing %pfw\n", dev_fwnode(dev));

	/* not devm: see yc_tpg_release() */
	tpg = kzalloc(sizeof(*tpg), GFP_KERNEL);
	if (!tpg)
		return -ENOMEM;
	tpg->dev = dev;

	ret = yc_tpg_read_interval(dev, tpg);
	if (ret) {
		kfree(tpg);
		return ret;
	}

	mutex_init(&tpg->lock);
	spin_lock_init(&tpg->qlock);
	INIT_LIST_HEAD(&tpg->buf_list);
	INIT_LIST_HEAD(&tpg->done_list);
	hrtimer_setup(&tpg->timer, yc_tpg_tick, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_SOFT);
	INIT_WORK(&tpg->fill_work, yc_tpg_fill_work);

	tpg->fmt.width = 640;
	tpg->fmt.height = 480;
	yc_tpg_fill_pix_format(&tpg->fmt);

	ret = v4l2_device_register(dev, &tpg->v4l2_dev);
	if (ret) {
		mutex_destroy(&tpg->lock);
		kfree(tpg);
		return ret;
	}
	/* from here on, every error path ends in v4l2_device_put() */
	tpg->v4l2_dev.release = yc_tpg_release;

	q = &tpg->queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->dev = dev;
	q->drv_priv = tpg;
	q->buf_struct_size = sizeof(struct yc_tpg_buffer);
	q->ops = &yc_tpg_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC |
			     V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
	q->min_queued_buffers = 1;
	q->lock = &tpg->lock;
	ret = vb2_queue_init(q);
	if (ret)
		goto err_put;

	vdev = &tpg->vdev;
	strscpy(vdev->name, "yongchun-tpg", sizeof(vdev->name));
	/* vdev is embedded in tpg and freed with it in yc_tpg_release() */
	vdev->release = video_device_release_empty;
	vdev->fops = &yc_tpg_fops;
	vdev->ioctl_ops = &yc_tpg_ioctl_ops;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	vdev->lock = &tpg->lock;
	vdev->queue = q;
	vdev->v4l2_dev = &tpg->v4l2_dev;
	video_set_drvdata(vdev, tpg);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_put;

	platform_set_drvdata(pdev, tpg);

	dev_info(dev, "%s: frame interval %u/%u s, period %llu ns\n",
		 video_device_node_name(vdev), tpg->interval[0],
		 tpg->interval[1], tpg->period_ns);
	return 0;

err_put:
	v4l2_device_put(&tpg->v4l2_dev);
	return ret;
}

static void yc_tpg_remove(struct platform_device *pdev)
{
	struct yc_tpg *tpg = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "removing %s\n",
		 video_device_node_name(&tpg->vdev));

	/* stops streaming now, even if the node is still open */
	vb2_video_unregister_device(&tpg->vdev);
	v4l2_device_put(&tpg->v4l2_dev);
}

static const struct of_device_id yc_tpg_of_match[] = {
	{ .compatible = "yongchun,tpg" },
	{ }
};
MODULE_DEVICE_TABLE(of, yc_tpg_of_match);

static struct platform_driver yc_tpg_driver = {
	.probe = yc_tpg_probe,
	.remove = yc_tpg_remove,
	.driver = {
		.name = "yongchun-tpg",
		.of_match_table = yc_tpg_of_match,
	},
};
module_platform_driver(yc_tpg_driver);

MODULE_AUTHOR("YP Chan <53651187+AsdreamChan@users.noreply.github.com>");
MODULE_DESCRIPTION("Test pattern generator V4L2 capture driver");
MODULE_LICENSE("GPL");
