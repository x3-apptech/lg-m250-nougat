#include <linux/init.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <soc/mediatek/lge/board_lge.h>

static hw_rev_type lge_bd_rev;

/* CAUTION: These strings are come from LK. */
char *rev_str[] = {
	"rev_0",
	"rev_0_1",
	"rev_a",
	"rev_b",
	"rev_c",
	"rev_10",
	"rev_11",
	"reserved",
};

static int __init board_revno_setup(char *rev_info)
{
	int i;
	lge_bd_rev	= HW_REV_0;
	for (i = 0; i <= HW_REV_MAX; i++)	{
		if(!strncmp(rev_info, rev_str[i], (strlen(rev_info) > strlen(rev_str[i])) ? strlen(rev_info) : strlen(rev_str[i])))	{
			lge_bd_rev	= (hw_rev_type) i;
			/* it is defined externally in <asm/system_info.h> */
			/* system_rev = lge_bd_rev; */
			break;
		}
	}

	printk(KERN_ERR "unified LK bootcmd lge.rev setup: %s\n", rev_str[lge_bd_rev]);

	return 1;
}
__setup("lge.rev=", board_revno_setup);

hw_rev_type lge_get_board_revno(void)
{
	return lge_bd_rev;
}

char *smpl_info = "true";

static int lge_mtk_smpl_check	= 0;

static int __init board_smpl_setup(char *smpl_info)
{
	lge_mtk_smpl_check	= 1;
	printk(KERN_ERR "unified LK bootcmd smpl boot setup\n");

	return 1;
}
__setup("smpl_boot=", board_smpl_setup);

int lge_get_mtk_smpl(void)
{
	return	lge_mtk_smpl_check;
}

#if defined CONFIG_LGD_INCELL_DB7400_HD_PH1
static int db7400_cut_ver;

static int __init db7400_cut_ver_setup(char *db7400_cut)
{
        sscanf(db7400_cut, "%d", &db7400_cut_ver);

        printk(KERN_ERR "unified LK bootcmd lge.db7400_cut_ver setup: %d\n", db7400_cut_ver);

        return 1;
}
__setup("lge.db7400_cut_ver=", db7400_cut_ver_setup);

int lge_get_db7400_cut_ver(void)
{
       return db7400_cut_ver;
}
#endif

#if defined(CONFIG_LGD_INCELL_LG4894_HD_LV5)
static int lcm_revision = 1;
static int __init lcm_revision_setup(char *value)
{

	int ret = 0;

	ret = kstrtoint(value, 10, &lcm_revision);
	if (!ret)
		printk("lcm_revision: %d \n", lcm_revision );
	else
		printk("lcm_revision: Couldn't get lcm revision%d \n", ret );

	return 1;
}
__setup("lcm_rev=", lcm_revision_setup);

int lge_get_lcm_revision(void)
{
    return lcm_revision;
}

static int lcm_maker_id = 0;
static int __init lcm_maker_id_setup(char *value)
{

        int ret = 0;

        ret = kstrtoint(value, 10, &lcm_maker_id);
        if (!ret)
                printk("lcm_maker_id: %d \n", lcm_maker_id);
        else
                printk("lcm_maker_id: Couldn't get lcm maker id %d \n", ret );

        return 1;
}
__setup("lcm_makerid=", lcm_maker_id_setup);

int lge_get_maker_id(void)
{
    return lcm_maker_id;
}
#endif

static enum lge_laf_mode_type lge_laf_mode = LGE_LAF_MODE_NORMAL;

int __init lge_laf_mode_init(char *s)
{
    if (strcmp(s, "") && strcmp(s, "MID"))
        lge_laf_mode = LGE_LAF_MODE_LAF;

    return 1;
}
__setup("androidboot.laf=", lge_laf_mode_init);

enum lge_laf_mode_type lge_get_laf_mode(void)
{
    return lge_laf_mode;
}

static bool is_mfts_mode = 0;
static int __init lge_mfts_mode_init(char *s)
{
        if(strncmp(s,"1",1) == 0)
                is_mfts_mode = 1;
        return 0;
}
__setup("mfts.mode=", lge_mfts_mode_init);

bool lge_get_mfts_mode(void)
{
        return is_mfts_mode;
}

/*
 * get from LG QCT source code
 */
static int lge_boot_reason = -1; /* undefined for error checking */
static int __init lge_check_bootreason(char *reason)
{
	int ret = 0;

#if 0
	/* handle corner case of kstrtoint */
	if (!strcmp(reason, "0xffffffff")) {
		lge_boot_reason = 0xffffffff;
		return 1;
	}

	ret = kstrtoint(reason, 16, &lge_boot_reason);
#endif

	ret = kstrtoint(reason, 0, &lge_boot_reason);
	if (!ret)
		printk(KERN_INFO "LGE REBOOT REASON: %x\n", lge_boot_reason);
	else
		printk(KERN_INFO "LGE REBOOT REASON: "
					"Couldn't get bootreason - %d\n", ret);

	return 1;
}
__setup("lge.bootreasoncode=", lge_check_bootreason);

int lge_get_bootreason(void)
{
	return lge_boot_reason;
}

int of_scan_meid_node(char *meid, int len)
{
	struct device_node *node;
	const char *tmp;

	node = of_find_node_by_path("/chosen");
	if (node == NULL) {
	pr_err("%s: chosen node not found\n", __func__);
		return 0;
	}

	if (of_property_read_string(node, "lge,meid", &tmp)) {
	pr_err("%s: lge,meid not found\n", __func__);
		return 0;
	}
	memcpy(meid, tmp, len);

	return 1;
}
EXPORT_SYMBOL(of_scan_meid_node);

static char model_name[20];

static int __init lge_model_name_init(char *m_name)
{
	if (strlen(m_name) == 0) {
		pr_info("Failed get model.name\n");
		return 0;
	}

	strncpy(model_name, m_name, strlen(m_name));
	pr_info("model.name = %s\n", model_name);

	return 1;
}
__setup("model.name=", lge_model_name_init);

char *lge_get_model_name(void)
{
	return model_name;
}
