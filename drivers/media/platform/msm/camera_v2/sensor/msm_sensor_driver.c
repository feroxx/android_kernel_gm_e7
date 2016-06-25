/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Header file declaration */
#include "msm_sensor.h"
#include "msm_sd.h"
#include "camera.h"
#include "msm_cci.h"
#include "msm_camera_dt_util.h"

/* Logging macro */
/*#define MSM_SENSOR_DRIVER_DEBUG*/
#undef CDBG
#ifdef MSM_SENSOR_DRIVER_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) pr_debug(fmt, ##args)
#endif

#define	SENSOR_MAX_MOUNTANGLE (360)

/* Static declaration */
static struct msm_sensor_ctrl_t *g_sctrl[MAX_CAMERAS];

static int msm_sensor_platform_remove(struct platform_device *pdev)
{
	struct msm_sensor_ctrl_t  *s_ctrl;

	pr_err("%s: sensor FREE\n", __func__);

	s_ctrl = g_sctrl[pdev->id];
	if (!s_ctrl) {
		pr_err("%s: sensor device is NULL\n", __func__);
		return 0;
	}

	msm_sensor_free_sensor_data(s_ctrl);
	kfree(s_ctrl->msm_sensor_mutex);
	kfree(s_ctrl->sensor_i2c_client);
	kfree(s_ctrl);
	g_sctrl[pdev->id] = NULL;

	return 0;
}


static const struct of_device_id msm_sensor_driver_dt_match[] = {
	{.compatible = "qcom,camera"},
	{}
};

MODULE_DEVICE_TABLE(of, msm_sensor_driver_dt_match);

static struct platform_driver msm_sensor_platform_driver = {
	.driver = {
		.name = "qcom,camera",
		.owner = THIS_MODULE,
		.of_match_table = msm_sensor_driver_dt_match,
	},
	.remove = msm_sensor_platform_remove,
};

static struct v4l2_subdev_info msm_sensor_driver_subdev_info[] = {
	{
		.code = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt = 1,
		.order = 0,
	},
};

static int32_t msm_sensor_fill_eeprom_subdevid_by_name(
				struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;
	const char *eeprom_name;
	struct device_node *src_node = NULL;
	uint32_t val = 0, count = 0, eeprom_name_len;
	int i;
	int32_t *eeprom_subdev_id;
	struct  msm_sensor_info_t *sensor_info;
	struct device_node *of_node = s_ctrl->of_node;
	const void *p;

	if (!s_ctrl->sensordata->eeprom_name || !of_node)
		return -EINVAL;

	eeprom_name_len = strlen(s_ctrl->sensordata->eeprom_name);
	if (eeprom_name_len >= MAX_SENSOR_NAME)
		return -EINVAL;

	sensor_info = s_ctrl->sensordata->sensor_info;
	eeprom_subdev_id = &sensor_info->subdev_id[SUB_MODULE_EEPROM];
	/*
	 * string for eeprom name is valid, set sudev id to -1
	 *  and try to found new id
	 */
	*eeprom_subdev_id = -1;

	if (0 == eeprom_name_len)
		return 0;

	CDBG("Try to find eeprom subdev for %s\n",
			s_ctrl->sensordata->eeprom_name);
	p = of_get_property(of_node, "qcom,eeprom-src", &count);
	if (!p || !count)
		return 0;

	count /= sizeof(uint32_t);
	for (i = 0; i < count; i++) {
		eeprom_name = NULL;
		src_node = of_parse_phandle(of_node, "qcom,eeprom-src", i);
		if (!src_node) {
			pr_err("eeprom src node NULL\n");
			continue;
		}
		rc = of_property_read_string(src_node, "qcom,eeprom-name",
			&eeprom_name);
		if (rc < 0) {
			pr_err("failed\n");
			of_node_put(src_node);
			continue;
		}
		if (strcmp(eeprom_name, s_ctrl->sensordata->eeprom_name))
			continue;

		rc = of_property_read_u32(src_node, "cell-index", &val);

		CDBG("%s qcom,eeprom cell index %d, rc %d\n", __func__,
			val, rc);
		if (rc < 0) {
			pr_err("failed\n");
			of_node_put(src_node);
			continue;
		}

		*eeprom_subdev_id = val;
		CDBG("Done. Eeprom subdevice id is %d\n", val);
		of_node_put(src_node);
		src_node = NULL;
		break;
	}

	return rc;
}

static int32_t msm_sensor_fill_actuator_subdevid_by_name(
				struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;
	struct device_node *src_node = NULL;
	uint32_t val = 0, actuator_name_len;
	int32_t *actuator_subdev_id;
	struct  msm_sensor_info_t *sensor_info;
	struct device_node *of_node = s_ctrl->of_node;

	if (!s_ctrl->sensordata->actuator_name || !of_node)
		return -EINVAL;

	actuator_name_len = strlen(s_ctrl->sensordata->actuator_name);
	if (actuator_name_len >= MAX_SENSOR_NAME)
		return -EINVAL;

	sensor_info = s_ctrl->sensordata->sensor_info;
	actuator_subdev_id = &sensor_info->subdev_id[SUB_MODULE_ACTUATOR];
	/*
	 * string for actuator name is valid, set sudev id to -1
	 * and try to found new id
	 */
	*actuator_subdev_id = -1;

	if (0 == actuator_name_len)
		return 0;

	src_node = of_parse_phandle(of_node, "qcom,actuator-src", 0);
	if (!src_node) {
		CDBG("%s:%d src_node NULL\n", __func__, __LINE__);
	} else {
		rc = of_property_read_u32(src_node, "cell-index", &val);
		CDBG("%s qcom,actuator cell index %d, rc %d\n", __func__,
			val, rc);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			return -EINVAL;
		}
		*actuator_subdev_id = val;
		of_node_put(src_node);
		src_node = NULL;
	}

	return rc;
}

static int32_t msm_sensor_fill_slave_info_init_params(
	struct msm_camera_sensor_slave_info *slave_info,
	struct msm_sensor_info_t *sensor_info)
{
	struct msm_sensor_init_params *sensor_init_params;
	if (!slave_info ||  !sensor_info)
		return -EINVAL;

	if (!slave_info->is_init_params_valid)
		return 0;

	sensor_init_params = &slave_info->sensor_init_params;
	if (INVALID_CAMERA_B != sensor_init_params->position)
		sensor_info->position =
			sensor_init_params->position;

	if (SENSOR_MAX_MOUNTANGLE > sensor_init_params->sensor_mount_angle) {
		sensor_info->sensor_mount_angle =
			sensor_init_params->sensor_mount_angle;
		sensor_info->is_mount_angle_valid = 1;
	}

	if (CAMERA_MODE_INVALID != sensor_init_params->modes_supported)
		sensor_info->modes_supported =
			sensor_init_params->modes_supported;

	return 0;
}


static int32_t msm_sensor_validate_slave_info(
	struct msm_sensor_info_t *sensor_info)
{
	if (INVALID_CAMERA_B == sensor_info->position) {
		sensor_info->position = BACK_CAMERA_B;
		pr_err("%s Set dafault sensor position%d\n",
			__func__, __LINE__);
	}
	if (CAMERA_MODE_INVALID == sensor_info->modes_supported) {
		sensor_info->modes_supported = CAMERA_MODE_2D_B;
		pr_err("%s Set dafault sensor modes_supported%d\n",
			__func__, __LINE__);
	}
	if (SENSOR_MAX_MOUNTANGLE < sensor_info->sensor_mount_angle) {
		sensor_info->sensor_mount_angle = 0;
		pr_err("%s Set dafault sensor mount angle%d\n",
			__func__, __LINE__);
		sensor_info->is_mount_angle_valid = 1;
	}
	return 0;
}

/* static function definition */
int32_t msm_sensor_driver_probe(void *setting)
{
	int32_t                              rc = 0;
	uint16_t                             i = 0, size = 0;
	uint32_t                             session_id = 0;
	struct msm_sensor_ctrl_t            *s_ctrl = NULL;
	struct msm_camera_cci_client        *cci_client = NULL;
	struct msm_camera_sensor_slave_info *slave_info = NULL;
	struct msm_sensor_power_setting     *power_setting = NULL;
	struct msm_camera_slave_info        *camera_info = NULL;
	struct msm_camera_power_ctrl_t      *power_info = NULL;
	unsigned long mount_pos = 0;

	/* Validate input parameters */
	if (!setting) {
		pr_err("failed: slave_info %p", setting);
		return -EINVAL;
	}

	/* Allocate memory for slave info */
	slave_info = kzalloc(sizeof(*slave_info), GFP_KERNEL);
	if (!slave_info) {
		pr_err("failed: no memory slave_info %p", slave_info);
		return -ENOMEM;
	}

	if (copy_from_user(slave_info, (void *)setting, sizeof(*slave_info))) {
		pr_err("failed: copy_from_user");
		rc = -EFAULT;
		goto FREE_SLAVE_INFO;
	}

	/* Print slave info */
	CDBG("camera id %d", slave_info->camera_id);
	CDBG("slave_addr %x", slave_info->slave_addr);
	CDBG("addr_type %d", slave_info->addr_type);
	CDBG("sensor_id_reg_addr %x",
		slave_info->sensor_id_info.sensor_id_reg_addr);
	CDBG("sensor_id %x", slave_info->sensor_id_info.sensor_id);
	CDBG("size %x", slave_info->power_setting_array.size);

	if (slave_info->is_init_params_valid) {
		CDBG("position %d",
			slave_info->sensor_init_params.position);
		CDBG("mount %d",
			slave_info->sensor_init_params.sensor_mount_angle);
	}

	/* Validate camera id */
	if (slave_info->camera_id >= MAX_CAMERAS) {
		pr_err("failed: invalid camera id %d max %d",
			slave_info->camera_id, MAX_CAMERAS);
		rc = -EINVAL;
		goto FREE_POWER_SETTING;
	}

	/* Extract s_ctrl from camera id */
	s_ctrl = g_sctrl[slave_info->camera_id];
	if (!s_ctrl) {
		pr_err("failed: s_ctrl %p for camera_id %d", s_ctrl,
			slave_info->camera_id);
		rc = -EINVAL;
		goto FREE_POWER_SETTING;
	}

	CDBG("s_ctrl[%d] %p", slave_info->camera_id, s_ctrl);

	if (s_ctrl->is_probe_succeed == 1) {
		/*
		 * Different sensor on this camera slot has been connected
		 * and probe already succeeded for that sensor. Ignore this
		 * probe
		 */
		pr_err("slot %d has some other sensor", slave_info->camera_id);
		kfree(slave_info);
		return 0;
	}

	size = slave_info->power_setting_array.size;
	/* Allocate memory for power setting */
	power_setting = kzalloc(sizeof(*power_setting) * size, GFP_KERNEL);
	if (!power_setting) {
		pr_err("failed: no memory power_setting %p", power_setting);
		rc = -ENOMEM;
		goto FREE_SLAVE_INFO;
	}

	if (copy_from_user(power_setting,
		(void *)slave_info->power_setting_array.power_setting,
		sizeof(*power_setting) * size)) {
		pr_err("failed: copy_from_user");
		rc = -EFAULT;
		goto FREE_POWER_SETTING;
	}

	/* Print power setting */
	for (i = 0; i < size; i++) {
		CDBG("seq_type %d seq_val %d config_val %ld delay %d",
			power_setting[i].seq_type, power_setting[i].seq_val,
			power_setting[i].config_val, power_setting[i].delay);
	}

	camera_info = kzalloc(sizeof(struct msm_camera_slave_info), GFP_KERNEL);
	if (!camera_info) {
		pr_err("failed: no memory slave_info %p", camera_info);
		goto FREE_POWER_SETTING;

	}

	/* Fill power up setting and power up setting size */
	power_info = &s_ctrl->sensordata->power_info;
	power_info->power_setting = power_setting;
	power_info->power_setting_size = size;

	s_ctrl->sensordata->slave_info = camera_info;

	/* Fill sensor slave info */
	camera_info->sensor_slave_addr = slave_info->slave_addr;
	camera_info->sensor_id_reg_addr =
		slave_info->sensor_id_info.sensor_id_reg_addr;
	camera_info->sensor_id = slave_info->sensor_id_info.sensor_id;

	/* Fill CCI master, slave address and CCI default params */
	if (!s_ctrl->sensor_i2c_client) {
		pr_err("failed: sensor_i2c_client %p",
			s_ctrl->sensor_i2c_client);
		rc = -EINVAL;
		goto FREE_POWER_SETTING;
	}
	/* Fill sensor address type */
	s_ctrl->sensor_i2c_client->addr_type = slave_info->addr_type;

	cci_client = s_ctrl->sensor_i2c_client->cci_client;
	if (!cci_client) {
		pr_err("failed: cci_client %p", cci_client);
		goto FREE_POWER_SETTING;
	}
	cci_client->cci_i2c_master = s_ctrl->cci_i2c_master;
	cci_client->sid = slave_info->slave_addr >> 1;
	cci_client->retries = 3;
	cci_client->id_map = 0;

	/* Parse and fill vreg params */
	rc = msm_camera_fill_vreg_params(
		power_info->cam_vreg,
		power_info->num_vreg,
		power_info->power_setting,
		power_info->power_setting_size);
	if (rc < 0) {
		pr_err("failed: msm_camera_get_dt_power_setting_data rc %d",
			rc);
		goto FREE_POWER_SETTING;
	}

	/* Update sensor name in sensor control structure */
	s_ctrl->sensordata->sensor_name = slave_info->sensor_name;
	s_ctrl->sensordata->eeprom_name = slave_info->eeprom_name;
	s_ctrl->sensordata->actuator_name = slave_info->actuator_name;

	/*
	 * Update eeporm subdevice Id by input eeprom name
	 */
	rc = msm_sensor_fill_eeprom_subdevid_by_name(s_ctrl);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_POWER_SETTING;
	}
	/*
	 * Update actuator subdevice Id by input actuator name
	 */
	rc = msm_sensor_fill_actuator_subdevid_by_name(s_ctrl);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto FREE_POWER_SETTING;
	}
 
	/* Power up and probe sensor */
	rc = s_ctrl->func_tbl->sensor_power_up(s_ctrl);
	if (rc < 0) {
		pr_err("%s power up failed", slave_info->sensor_name);
		goto FREE_POWER_SETTING;
	}

	pr_err("%s probe succeeded", slave_info->sensor_name);

	/*
	  Set probe succeeded flag to 1 so that no other camera shall
	 * probed on this slot
	 */
	s_ctrl->is_probe_succeed = 1;

	/*
	 * Create /dev/videoX node, comment for now until dummy /dev/videoX
	 * node is created and used by HAL
	 */
	rc = camera_init_v4l2(&s_ctrl->pdev->dev, &session_id);
	if (rc < 0) {
		pr_err("failed: camera_init_v4l2 rc %d", rc);
		goto FREE_POWER_SETTING;
	}
	CDBG("rc %d session_id %d", rc, session_id);
	s_ctrl->sensordata->sensor_info->session_id = session_id;

	/* Create /dev/v4l-subdevX device */
	v4l2_subdev_init(&s_ctrl->msm_sd.sd, s_ctrl->sensor_v4l2_subdev_ops);
	snprintf(s_ctrl->msm_sd.sd.name, sizeof(s_ctrl->msm_sd.sd.name), "%s",
		s_ctrl->sensordata->sensor_name);
	v4l2_set_subdevdata(&s_ctrl->msm_sd.sd, s_ctrl->pdev);
	s_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	media_entity_init(&s_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	s_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	s_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_SENSOR;
	s_ctrl->msm_sd.sd.entity.name = s_ctrl->msm_sd.sd.name;
	s_ctrl->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x3;
	msm_sd_register(&s_ctrl->msm_sd);

	/* Power down */
	s_ctrl->func_tbl->sensor_power_down(s_ctrl);

	rc = msm_sensor_fill_slave_info_init_params(
		slave_info,
		s_ctrl->sensordata->sensor_info);
	if (rc < 0) {
		pr_err("%s Fill slave info failed", slave_info->sensor_name);
		goto FREE_CAMERA_INFO;
	}
	rc = msm_sensor_validate_slave_info(s_ctrl->sensordata->sensor_info);
	if (rc < 0) {
		pr_err("%s Validate slave info failed",
			slave_info->sensor_name);
		goto FREE_CAMERA_INFO;
	}
	/* Update sensor mount angle and position in media entity flag */
	mount_pos = s_ctrl->sensordata->sensor_info->position << 16;
	mount_pos = mount_pos | ((s_ctrl->sensordata->sensor_info->
		sensor_mount_angle / 90) << 8);
	s_ctrl->msm_sd.sd.entity.flags = mount_pos | MEDIA_ENT_FL_DEFAULT;

	return rc;

FREE_POWER_SETTING:
	kfree(power_setting);
FREE_SLAVE_INFO:
	kfree(slave_info);
	return rc;
}

static int32_t msm_sensor_driver_get_gpio_data(
	struct msm_camera_sensor_board_info *sensordata,
	struct device_node *of_node)
{
	int32_t                      rc = 0, i = 0;
	struct msm_camera_gpio_conf *gconf = NULL;
	uint16_t                    *gpio_array = NULL;
	uint16_t                     gpio_array_size = 0;

	/* Validate input paramters */
	if (!sensordata || !of_node) {
		pr_err("failed: invalid params sensordata %p of_node %p",
			sensordata, of_node);
		return -EINVAL;
	}

	sensordata->power_info.gpio_conf = kzalloc(
			sizeof(struct msm_camera_gpio_conf), GFP_KERNEL);
	if (!sensordata->power_info.gpio_conf) {
		pr_err("failed");
		return -ENOMEM;
	}
	gconf = sensordata->power_info.gpio_conf;

	gpio_array_size = of_gpio_count(of_node);
	CDBG("gpio count %d", gpio_array_size);
	if (!gpio_array_size)
		return 0;

	gpio_array = kzalloc(sizeof(uint16_t) * gpio_array_size, GFP_KERNEL);
	if (!gpio_array) {
		pr_err("failed");
		goto FREE_GPIO_CONF;
	}
	for (i = 0; i < gpio_array_size; i++) {
		gpio_array[i] = of_get_gpio(of_node, i);
		CDBG("gpio_array[%d] = %d", i, gpio_array[i]);
	}

	rc = msm_camera_get_dt_gpio_req_tbl(of_node, gconf, gpio_array,
		gpio_array_size);
	if (rc < 0) {
		pr_err("failed");
		goto FREE_GPIO_CONF;
	}

	rc = msm_camera_init_gpio_pin_tbl(of_node, gconf, gpio_array,
		gpio_array_size);
	if (rc < 0) {
		pr_err("failed");
		goto FREE_GPIO_REQ_TBL;
	}

	kfree(gpio_array);
	return rc;

FREE_GPIO_REQ_TBL:
	kfree(sensordata->power_info.gpio_conf->cam_gpio_req_tbl);
FREE_GPIO_CONF:
	kfree(sensordata->power_info.gpio_conf);
	kfree(gpio_array);
	return rc;
}

static int32_t msm_sensor_driver_get_dt_data(struct msm_sensor_ctrl_t *s_ctrl,
	struct platform_device *pdev)
{
	int32_t                              rc = 0;
	struct msm_camera_sensor_board_info *sensordata = NULL;
	struct device_node                  *of_node = pdev->dev.of_node;

	s_ctrl->sensordata = kzalloc(sizeof(*sensordata), GFP_KERNEL);
	if (!s_ctrl->sensordata) {
		pr_err("failed: no memory");
		return -ENOMEM;
	}

	sensordata = s_ctrl->sensordata;

	/*
	 * Read cell index - this cell index will be the camera slot where
	 * this camera will be mounted
	 */
	rc = of_property_read_u32(of_node, "cell-index", &pdev->id);
	if (rc < 0) {
		pr_err("failed: cell-index rc %d", rc);
		goto FREE_SENSOR_DATA;
	}

	/* Validate pdev->id */
	if (pdev->id >= MAX_CAMERAS) {
		pr_err("failed: invalid pdev->id %d", pdev->id);
		rc = -EINVAL;
		goto FREE_SENSOR_DATA;
	}

	/* Check whether g_sctrl is already filled for this pdev id */
	if (g_sctrl[pdev->id]) {
		pr_err("failed: sctrl already filled for id %d", pdev->id);
		rc = -EINVAL;
		goto FREE_SENSOR_DATA;
	}

	/* Read subdev info */
	rc = msm_sensor_get_sub_module_index(of_node, &sensordata->sensor_info);
	if (rc < 0) {
		pr_err("failed");
		goto FREE_SENSOR_DATA;
	}

	/* Read vreg information */
	rc = msm_camera_get_dt_vreg_data(of_node,
		&sensordata->power_info.cam_vreg,
		&sensordata->power_info.num_vreg);
	if (rc < 0) {
		pr_err("failed: msm_camera_get_dt_vreg_data rc %d", rc);
		goto FREE_SUB_MODULE_DATA;
	}

	/* Read gpio information */
	rc = msm_sensor_driver_get_gpio_data(sensordata, of_node);
	if (rc < 0) {
		pr_err("failed: msm_sensor_driver_get_gpio_data rc %d", rc);
		goto FREE_VREG_DATA;
	}

	/* Get CCI master */
	rc = of_property_read_u32(of_node, "qcom,cci-master",
		&s_ctrl->cci_i2c_master);
	CDBG("qcom,cci-master %d, rc %d", s_ctrl->cci_i2c_master, rc);
	if (rc < 0) {
		/* Set default master 0 */
		s_ctrl->cci_i2c_master = MASTER_0;
		rc = 0;
	}

	/* Get mount angle */

	rc = of_property_read_u32(of_node, "qcom,mount-angle",
		&sensordata->sensor_info->sensor_mount_angle);
	CDBG("%s qcom,mount-angle %d, rc %d\n", __func__,
		sensordata->sensor_info->sensor_mount_angle, rc);
	if (rc < 0) {
		/* Invalidate mount angle flag */
		sensordata->sensor_info->is_mount_angle_valid = 0;
		sensordata->sensor_info->sensor_mount_angle = 0;
		rc = 0;
	} else {
		sensordata->sensor_info->is_mount_angle_valid = 1;
	}

	rc = of_property_read_u32(of_node, "qcom,sensor-position",
		&sensordata->sensor_info->position);
	if (rc < 0) {
		pr_err("%s:%d Invalid sensor position\n", __func__, __LINE__);
		sensordata->sensor_info->position = INVALID_CAMERA_B;
		rc = 0;
	}

	rc = of_property_read_u32(of_node, "qcom,sensor-mode",
		&sensordata->sensor_info->modes_supported);
	if (rc < 0) {
		pr_err("%s:%d Invalid sensor mode supported\n",
			__func__, __LINE__);
		sensordata->sensor_info->modes_supported = CAMERA_MODE_INVALID;
		rc = 0;
	}

	/* Get vdd-cx regulator */
	/*Optional property, don't return error if absent */
	of_property_read_string(of_node, "qcom,vdd-cx-name",
		&sensordata->misc_regulator);
	CDBG("qcom,misc_regulator %s", sensordata->misc_regulator);

	return rc;

FREE_VREG_DATA:
	kfree(sensordata->power_info.cam_vreg);
FREE_SUB_MODULE_DATA:
	kfree(sensordata->sensor_info);
FREE_SENSOR_DATA:
	kfree(sensordata);
	return rc;
}

static int32_t msm_sensor_driver_parse(struct platform_device *pdev)
{
	int32_t                   rc = 0;
	struct msm_sensor_ctrl_t *s_ctrl = NULL;

	CDBG("Enter");
	/* Validate input parameters */
	if (!pdev || !pdev->dev.of_node) {
		pr_err("failed: invalid params");
		return -EINVAL;
	}

	/* Create sensor control structure */
	s_ctrl = kzalloc(sizeof(*s_ctrl), GFP_KERNEL);
	if (!s_ctrl) {
		pr_err("failed: no memory s_ctrl %p", s_ctrl);
		return -ENOMEM;
	}

	/* Fill platform device */
	s_ctrl->pdev = pdev;

	/* Allocate memory for sensor_i2c_client */
	s_ctrl->sensor_i2c_client = kzalloc(sizeof(*s_ctrl->sensor_i2c_client),
		GFP_KERNEL);
	if (!s_ctrl->sensor_i2c_client) {
		pr_err("failed: no memory sensor_i2c_client %p",
			s_ctrl->sensor_i2c_client);
		goto FREE_SCTRL;
	}

	/* Allocate memory for mutex */
	s_ctrl->msm_sensor_mutex = kzalloc(sizeof(*s_ctrl->msm_sensor_mutex),
		GFP_KERNEL);
	if (!s_ctrl->msm_sensor_mutex) {
		pr_err("failed: no memory msm_sensor_mutex %p",
			s_ctrl->msm_sensor_mutex);
		goto FREE_SENSOR_I2C_CLIENT;
	}

	/* Parse dt information and store in sensor control structure */
	rc = msm_sensor_driver_get_dt_data(s_ctrl, pdev);
	if (rc < 0) {
		pr_err("failed: rc %d", rc);
		goto FREE_MUTEX;
	}

	/* Fill device in power info */
	s_ctrl->sensordata->power_info.dev = &pdev->dev;

	/* Initialize mutex */
	mutex_init(s_ctrl->msm_sensor_mutex);

	/* Initilize v4l2 subdev info */
	s_ctrl->sensor_v4l2_subdev_info = msm_sensor_driver_subdev_info;
	s_ctrl->sensor_v4l2_subdev_info_size =
		ARRAY_SIZE(msm_sensor_driver_subdev_info);

	/* Initialize default parameters */
	rc = msm_sensor_init_default_params(s_ctrl);
	if (rc < 0) {
		pr_err("failed: msm_sensor_init_default_params rc %d", rc);
		goto FREE_DT_DATA;
	}

	/* Store sensor control structure in static database */
	g_sctrl[pdev->id] = s_ctrl;
	pr_err("g_sctrl[%d] %p", pdev->id, g_sctrl[pdev->id]);

	return rc;

FREE_DT_DATA:
	kfree(s_ctrl->sensordata->power_info.gpio_conf->gpio_num_info);
	kfree(s_ctrl->sensordata->power_info.gpio_conf->cam_gpio_req_tbl);
	kfree(s_ctrl->sensordata->power_info.gpio_conf);
	kfree(s_ctrl->sensordata->power_info.cam_vreg);
	kfree(s_ctrl->sensordata);
FREE_MUTEX:
	kfree(s_ctrl->msm_sensor_mutex);
FREE_SENSOR_I2C_CLIENT:
	kfree(s_ctrl->sensor_i2c_client);
FREE_SCTRL:
	kfree(s_ctrl);
	return rc;
}

static int __init msm_sensor_driver_init(void)
{
	int32_t rc = 0;

	CDBG("Enter");
	rc = platform_driver_probe(&msm_sensor_platform_driver,
		msm_sensor_driver_parse);
	if (!rc)
		pr_info("probe success");
	return rc;
}


static void __exit msm_sensor_driver_exit(void)
{
	CDBG("Enter");
	platform_driver_unregister(&msm_sensor_platform_driver);
	return;
}

module_init(msm_sensor_driver_init);
module_exit(msm_sensor_driver_exit);
MODULE_DESCRIPTION("msm_sensor_driver");
MODULE_LICENSE("GPL v2");
