/* 
	Netfilter module for computing Qsize 
	Usage: insmod netfiltermod-ex.ko outputRate_eth1=1000 outputRate_eth2=1000
	outputRate_eth1 represents Output Link Capacity of eth1.
*/

#ifndef __KERNEL__
        #define __KERNEL__
#endif
#ifndef MODULE
        #define MODULE
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <asm/io.h>
#include <linux/inet.h>
#include <linux/vmalloc.h>
#include <net/ip.h>

#include <net/route.h>

#include <linux/spinlock.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
#include <linux/in.h>
#include <linux/ip.h>
#endif

#define NB_INTFS 3
#define NB_STATS 4

/* Args */
static int outputRate_eth1 = 1000; 
static int outputRate_eth2 = 10; 
module_param(outputRate_eth1, int, 0);
module_param(outputRate_eth2, int, 0);

static DEFINE_SPINLOCK(lock); 

static int stats_packets[NB_INTFS][NB_STATS] = { { 0 } };

/* This is the structure we shall use to register our function */
static struct nf_hook_ops nfho_forward;
static struct nf_hook_ops nfho_post;
static struct nf_hook_ops nfho_pre;

/* Name of the interface we want to drop packets from */
struct sk_buff *sock_buff;
struct iphdr *ip_header;
struct tcphdr *tcp_header;

inline int hash_index(const char* name_intf) { 
	return name_intf[strlen(name_intf) - 1] - '0'; 
}

/* Hook function: FORWARD */
unsigned int hook_func_forward(unsigned int hooknum, struct sk_buff *skb, const struct net_device *in, const struct net_device *out, int (*okfn)(struct sk_buff *)) {

	sock_buff = skb; // Ok, useless but ...
	
	if (!sock_buff) { 
		return NF_ACCEPT; 
	}
	ip_header = (struct iphdr *) skb_network_header (sock_buff);
	if (!ip_header) { 
		return NF_ACCEPT; 
	}
	if (ip_header->protocol != 6) { 
		return NF_ACCEPT; 
	}
	tcp_header = (struct tcphdr *) skb_transport_header (sock_buff);
	if (!tcp_header) { return NF_ACCEPT; }
	
	return NF_ACCEPT;
}

/* Hook function: POST */
unsigned int hook_func_post(unsigned int hooknum, struct sk_buff *skb, const struct net_device *in, const struct net_device *out, int (*okfn)(struct sk_buff *)) {

	int index;
	sock_buff = skb; // Ok, useless but ...
	
	if (!sock_buff) { 
		return NF_ACCEPT; }
	ip_header = (struct iphdr *) skb_network_header (sock_buff);
	if (!ip_header) { 
		return NF_ACCEPT; 
	}
	if (ip_header->protocol != 6) { 
		return NF_ACCEPT; 
	}
	tcp_header = (struct tcphdr *) skb_transport_header (sock_buff);
	if (!tcp_header) { 
		return NF_ACCEPT; 
	}
	
	printk(KERN_CRIT "POST");
	printk(KERN_CRIT "PostIn: %s | PostOut: %s", in->name, out->name);

	index = hash_index(out->name);

	spin_lock_irq(&lock);
	stats_packets[index][1]++; // O++
	stats_packets[index][2] = stats_packets[index][0] - stats_packets[index][1]; // qsize = I - O
	printk(KERN_CRIT "OutPkt: %d\n", stats_packets[index][1]);
	printk(KERN_CRIT "Qsize: %d\n", stats_packets[index][2]);
	spin_unlock_irq(&lock);

	return NF_ACCEPT;
}

/* Hook function: PRE */
unsigned int hook_func_pre(unsigned int hooknum, struct sk_buff *skb, const struct net_device *in, const struct net_device *out, int (*okfn)(struct sk_buff *)) {

	int rc = 51;
	int index = 42;
	__be32 src, dst;
	u8 tos;

	// Ok, useless but ...
	sock_buff = skb;
	
	if (!sock_buff) { 
		return NF_ACCEPT; 
	}
	ip_header = (struct iphdr *) skb_network_header (sock_buff);
	if (!(ip_header)) { 
		return NF_ACCEPT; 
	}
	if (ip_header->protocol != 6) { 
		return NF_ACCEPT; 
	}
	tcp_header = (struct tcphdr *) skb_transport_header (sock_buff);
	if (!tcp_header) { 
		return NF_ACCEPT; 
	}
	
	printk(KERN_CRIT "\nPRE");
	printk(KERN_CRIT "PreIn: %s | PreOut: %s", in->name, out->name);

	// Tests routage
	src = ip_header->saddr;
	dst = ip_header->daddr;
	tos = ip_header->tos;
	rc = ip_route_input(skb, dst, src, tos, in);
	/* printk(KERN_CRIT "Route: %d", rc); */

	if (ip_header->daddr == 29935816) { // Target: 200.200.200.1 via eth2
		index = hash_index("eth2");
	} else if (ip_header->daddr == 23356516) { // Target: 100.100.100.1 via eth1
		index = hash_index("eth1");
	}
	if (index == 42) {
		return NF_ACCEPT;
	}
	
	spin_lock_irq(&lock);
	stats_packets[index][0]++;
	printk(KERN_CRIT "InputPkts: %d\n", stats_packets[index][0]);
	spin_unlock_irq(&lock);

	return NF_ACCEPT;
}

/* Initialisation routine */
int init_module()
{

	/* Fill in our hook structure */
	nfho_forward.hook     = hook_func_forward;         /* Handler function */
	nfho_forward.hooknum  = NF_INET_FORWARD; /* NF_IP_FORWARD */
	nfho_forward.pf       = PF_INET;
	nfho_forward.priority = NF_IP_PRI_FIRST;   /* Make our function first */

	/* Fill in our hook structure */
	nfho_post.hook     = hook_func_post;         /* Handler function */
	nfho_post.hooknum  = NF_INET_POST_ROUTING; /* NF_IP_FORWARD */
	nfho_post.pf       = PF_INET;
	nfho_post.priority = NF_IP_PRI_FIRST;   /* Make our function first */

	/* Fill in our hook structure */
	nfho_pre.hook     = hook_func_pre;         /* Handler function */
	nfho_pre.hooknum  = NF_INET_PRE_ROUTING; /* NF_IP_FORWARD */
	nfho_pre.pf       = PF_INET;
	nfho_pre.priority = NF_IP_PRI_FIRST;   /* Make our function first */

	printk(KERN_CRIT "Go ahead\n");
	nf_register_hook(&nfho_forward);
	nf_register_hook(&nfho_post);
	nf_register_hook(&nfho_pre);

	stats_packets[1][3] = outputRate_eth1;
	stats_packets[2][3] = outputRate_eth2;

	return 0;
}
/* Cleanup routine */
void cleanup_module()
{
	nf_unregister_hook(&nfho_forward);
	nf_unregister_hook(&nfho_post);
	nf_unregister_hook(&nfho_pre);
}

MODULE_LICENSE("GPL");
