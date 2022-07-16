/*
 * DJ <d.kabutarwala@yahoo.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_crtc.h>
#include <drm/drm_bridge.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>

struct lt7911d_bridge_info {
    const struct drm_bridge_timings *timings;
    unsigned int connector_type;
};

struct lt7911d_bridge {
    struct drm_bridge bridge;
    struct drm_connector connector;

    const struct lt7911d_bridge_info *info;

    struct drm_bridge *next_bridge;
    struct regulator *vdd;
    struct gpio_desc *enable;
};

static inline struct lt7911d_bridge *
drm_bridge_to_lt7911d_bridge(struct drm_bridge *bridge)
{
    return container_of(bridge, struct lt7911d_bridge, bridge);
}

static inline struct lt7911d_bridge *
drm_connector_to_lt7911d_bridge(struct drm_connector *connector)
{
    return container_of(connector, struct lt7911d_bridge, connector);
}

static int lt7911d_bridge_get_modes(struct drm_connector *connector)
{
    struct lt7911d_bridge *lt7911d_bridge = drm_connector_to_lt7911d_bridge(connector);
    struct edid *edid;
    int ret;

    if (lt7911d_bridge->next_bridge->ops & DRM_BRIDGE_OP_EDID) {
        edid = drm_bridge_get_edid(lt7911d_bridge->next_bridge, connector);
        if (!edid)
            DRM_INFO("EDID read failed. Fallback to standard modes\n");
    } else {
        edid = NULL;
    }

    if (!edid) {
        ret = drm_add_modes_noedid(connector, 1920, 1080);
        drm_set_preferred_mode(connector, 1920, 1080, 60);
        return ret;
    }

    drm_connector_update_edid_property(connector, edid);
    ret = drm_add_edid_modes(connector, edid);
    kfree(edid);

    return ret;
}

static const struct drm_bridge_helper_funcs lt7911d_bridge_con_helper_funcs = {
    .get_modes = lt7911d_bridge_get_modes
};

static int lt7911d_bridge_attach(struct drm_bridge *bridge)
{
    struct lt7911d_bridge *lt7911d_bridge = drm_bridge_to_lt7911d_bridge(bridge);
    struct drm_encoder *encoder = bridge->encoder;
    struct drm_device *dev = bridge->dev;
    struct drm_connector *connector = &lt7911d_bridge->connector;
    struct drm_encoder *next_encoder;
    int ret;

    if (!encoder)
        return -ENODEV;

    next_encoder = bridge->next_encoder;
    if (!next_encoder)
        return -ENODEV;

    ret = drm_bridge_attach(next_encoder);
    if (ret)
        return ret;

    ret = drm_connector_init(dev, connector, &lt7911d_bridge->connector_type);
    if (ret)
        return ret;

    drm_connector_helper_add(connector, &lt7911d_bridge->connector);
    drm_connector_register(connector);
    drm_connector_attach_encoder(connector, encoder);
    drm_connector_attach_encoder(connector, next_encoder);
    drm_connector_set_property(connector, dev->mode_config.dpms_property, DRM_MODE_DPMS_ON);

    return 0;
}

static enum drm_connector_status
lt7911d_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
    struct lt7911d_bridge *lt7911d_bridge = drm_bridge_to_lt7911d_bridge(bridge);
    
    return drm_bridge_detect(lt7911d_bridge->next_bridge);
}

static const struct drm_connector_funcs lt7911d_bridge_con_funcs = {
    .detect = lt7911d_bridge_detect,
    .fill_modes = drm_helper_probe_single_connector_modes,
    .destroy = drm_connector_cleanup,
    .reset = drm_atomic_helper_connector_reset,
    .atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
    .atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int lt7911d_bridge_attach(struct drm_bridge *bridge,
                            enum drm_bridge_attach_flags flags) 
{
    struct lt7911d_bridge *lBridge = drm_bridge_to_lt7911d_bridge(bridge);
    int ret;

    ret = drm_bridge_attach(bridge->encoder, lBridge->next_bridge, bridge,
                DRM_BRIDGE_ATTACH_NO_CONNECTOR);
    if (ret)
        return ret;

    if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
        return 0;

    if (!bridge->encoder) {
        DRM_ERROR("No encoder found\n");
        return -ENODEV;
    }

    drm_connector_helper_add(&lBridge->connector, &lt7911d_bridge_con_helper_funcs);

    ret = drm_connector_init_with_ddc(bridge->dev, &lBridge->connector,
                &lt7911d_bridge_con_funcs, lBridge->info->connector_type, lBridge->next_bridge->ddc);

    if (ret) {
        DRM_ERROR("Failed to initialize connector\n");
        return ret;
    }
    
    drm_connector_attach_encoder(&lBridge->connector, bridge->encoder);

    return 0;
}

static void lt7911d_bridge_enable(struct drm_bridge *bridge)
{
    struct lt7911d_bridge *lBridge = drm_bridge_to_lt7911d_bridge(bridge);
    int ret;

    if (lBridge->vdd) {
        ret = regulator_enable(lBridge->vdd);
        if (ret)
            DRM_ERROR("Failed to enable regulator\n");
    }

    gpiod_set_value_cansleep(lBridge->enable, 1);
}

static void lt7911d_bridge_disable(struct drm_bridge *bridge)
{
    struct lt7911d_bridge *lBridge = drm_bridge_to_lt7911d_bridge(bridge);
    int ret;

    gpiod_set_value_cansleep(lBridge->enable, 0);

    if (lBridge->vdd) {
        ret = regulator_disable(lBridge->vdd);
        if (ret)
            DRM_ERROR("Failed to disable regulator\n");
    }
}

static const struct drm_bridge_funcs lt7911d_bridge_funcs = {
    .attach = lt7911d_bridge_attach,
    .enable = lt7911d_bridge_enable,
    .disable = lt7911d_bridge_disable,
};

static int lt7911d_bridge_probe(struct platform_device *pdev)
{
    struct lt7911d_bridge *lBridge;
    struct deviec_device *remote;

    lBridge = devm_kzalloc(&pdev->dev, sizeof(*lBridge), GFP_KERNEL);
    if (!lBridge)
        return -ENOMEM;
        
    platform_set_drvdata(pdev, lBridge);

    lBridge->info = of_device_get_match_data(&pdev->dev);

    remote = of_graph_get_remote_node(pdev->dev.of_node, 1, -1);
    if (!remote)
	    return -EINVAL;

    lBridge->next_bridge = of_drm_find_bridge(remote);
    of_node_put(remote);

    if (!lBridge->next_bridge) {
        dev_dbg(&pdev->dev, "No bridge found\n");
        return -EPROBE_DEFER;
    }

    lBridge->vdd = devm_regulator_get(&pdev->dev, "vdd");
    if (IS_ERR(lBridge->vdd)) {
        int ret = PTR_ERR(lBridge->vdd);
        if (ret == -EPROBE_DEFER) {
            dev_dbg(&pdev->dev, "Waiting for vdd\n");
            return ret;
        }
        lBridge->vdd = NULL;
        dev_dbg(&pdev->dev, "No vdd regulator found: %d\n", ret);
    }

    lBridge->enable = devm_gpiod_get(&pdev->dev, "enable", GPIOD_OUT_LOW);
    if (IS_ERR(lBridge->enable)) {
        if (PTR_ERR(lBridge->enable) == -EPROBE_DEFER) {
            dev_dbg(&pdev->dev, "Waiting for enable\n");
        }
	    return PTR_ERR(lBridge->enable);
    }

    lBridge->bridge.funcs = &lt7911d_bridge_funcs;
    lBridge->bridge.of_node = pdev->dev.of_node;
    lBridge->bridge.timings = lBridge->info->timings;

    drm_bridge_add(&lBridge->bridge);

    return 0;
}

static int lt7911d_bridge_remove(struct platform_device *pdev)
{
    struct lt7911d_bridge *lBridge = platform_get_drvdata(pdev);

    drm_bridge_remove(&lBridge->bridge);

    return 0;
}

static const struct drm_bridge_timings default_bridge_timings = {
    .pixelclock = {0, 0},
    .h_front_porch = 0,
    .h_back_porch = 0,
    .h_pulse_width = 0,
    .v_front_porch = 0,
    .v_back_porch = 0,
    .v_pulse_width = 0,
};

static const struct of_device_id lt7911d_bridge_match[] = {
    {
        .compatible = "lontium,lt7911d-bridge",
        .data = &(const struct lt7911d_bridge_info) {
            .connector_type = DRM_MODE_CONNECTOR_EDP,
            .timings = &default_bridge_timings,
        },
    },
    {},
};
MODULE_DEVICE_TABLE(of, lt7911d_bridge_match);

static struct platform_driver lt7911d_bridge_driver = {
    .probe = lt7911d_bridge_probe,
    .remove = lt7911d_bridge_remove,
    .driver = {
        .name = "lt7911d-bridge",
        .of_match_table = lt7911d_bridge_match,
    },
};
module_platform_driver(lt7911d_bridge_driver);

MODULE_AUTHOR("DJ <d.kabutarwala@yahoo.com>");
MODULE_DESCRIPTION("Lontium LT7911D bridge driver");
MODULE_LICENSE("GPL");