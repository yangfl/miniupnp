/* $Id: upnppermissions.c,v 1.20 2020/10/30 21:37:35 nanard Exp $ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or https://miniupnp.tuxfamily.org/
 * (c) 2006-2020 Thomas Bernard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#include <ctype.h>
#include <regex.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "config.h"
#include "upnppermissions.h"

/* read_permission_line()
 * parse the a permission line which format is :
 * (deny|allow) [0-9]+(-[0-9]+) ip/mask [0-9]+(-[0-9]+)
 * ip/mask is either 192.168.1.1/24 or 192.168.1.1/255.255.255.0
 */
int
read_permission_line(struct upnpperm * perm,
                     char * p)
{
	char * q;
	int n_bits;
	int i;

	/* first token: (allow|deny) */
	while(isspace(*p))
		p++;
	if(0 == memcmp(p, "allow", 5))
	{
		perm->type = UPNPPERM_ALLOW;
		p += 5;
	}
	else if(0 == memcmp(p, "deny", 4))
	{
		perm->type = UPNPPERM_DENY;
		p += 4;
	}
	else
	{
		return -1;
	}
	while(isspace(*p))
		p++;

	/* second token: eport or eport_min-eport_max */
	if(!isdigit(*p))
		return -1;
	for(q = p; isdigit(*q); q++);
	if(*q=='-')
	{
		*q = '\0';
		i = atoi(p);
		if(i > 65535)
			return -1;
		perm->eport_min = (u_short)i;
		q++;
		p = q;
		while(isdigit(*q))
			q++;
		*q = '\0';
		i = atoi(p);
		if(i > 65535)
			return -1;
		perm->eport_max = (u_short)i;
		if(perm->eport_min > perm->eport_max)
			return -1;
	}
	else if(isspace(*q))
	{
		*q = '\0';
		i = atoi(p);
		if(i > 65535)
			return -1;
		perm->eport_min = perm->eport_max = (u_short)i;
	}
	else
	{
		return -1;
	}
	p = q + 1;
	while(isspace(*p))
		p++;

	/* third token:  ip/mask */
	if(!isdigit(*p))
		return -1;
	for(q = p; isdigit(*q) || (*q == '.'); q++);
	if(*q=='/')
	{
		*q = '\0';
		if(!inet_aton(p, &perm->address))
			return -1;
		q++;
		p = q;
		while(isdigit(*q))
			q++;
		if(*q == '.')
		{
			while(*q == '.' || isdigit(*q))
				q++;
			if(!isspace(*q))
				return -1;
			*q = '\0';
			if(!inet_aton(p, &perm->mask))
				return -1;
		}
		else if(!isspace(*q))
			return -1;
		else
		{
			*q = '\0';
			n_bits = atoi(p);
			if(n_bits > 32)
				return -1;
			perm->mask.s_addr = htonl(n_bits ? (0xffffffffu << (32 - n_bits)) : 0);
		}
	}
	else if(isspace(*q))
	{
		*q = '\0';
		if(!inet_aton(p, &perm->address))
			return -1;
		perm->mask.s_addr = 0xffffffffu;
	}
	else
	{
		return -1;
	}
	p = q + 1;

	/* fourth token: iport or iport_min-iport_max */
	while(isspace(*p))
		p++;
	if(!isdigit(*p))
		return -1;
	for(q = p; isdigit(*q); q++);
	if(*q=='-')
	{
		*q = '\0';
		i = atoi(p);
		if(i > 65535)
			return -1;
		perm->iport_min = (u_short)i;
		q++;
		p = q;
		while(isdigit(*q))
			q++;
		*q = '\0';
		i = atoi(p);
		if(i > 65535)
			return -1;
		perm->iport_max = (u_short)i;
		if(perm->iport_min > perm->iport_max)
			return -1;
	}
	else if(isspace(*q) || *q == '\0')
	{
		*q = '\0';
		i = atoi(p);
		if(i > 65535)
			return -1;
		perm->iport_min = perm->iport_max = (u_short)i;
	}
	else
	{
		return -1;
	}

	/* fifth token: (optional) regex */
	while(isspace(*p))
		p++;
	if(*p == '\0')
		perm->re = NULL;
	else
	{
		int err;
		perm->re = strdup(p);
		if(!perm->re)
		{
			fprintf(stderr, "err when copying regex \"%s\": out of memory\n", p);
			perm->re = NULL;
			return -1;
		}
		/* icase: if case matters, it must be someone doing something nasty */
		err = regcomp(&perm->regex, p, REG_EXTENDED | REG_ICASE | REG_NOSUB);
		if(err)
		{
			char errbuf[256];
			regerror(err, &perm->regex, errbuf, sizeof(errbuf));
			fprintf(stderr, "err when compiling regex \"%s\": %s\n", p, errbuf);
			free(perm->re);
			perm->re = NULL;
			return -1;
		}
	}

#ifdef DEBUG
	printf("perm rule added : %s %hu-%hu %08x/%08x %hu-%hu %s\n",
	       (perm->type==UPNPPERM_ALLOW)?"allow":"deny",
	       perm->eport_min, perm->eport_max, ntohl(perm->address.s_addr),
	       ntohl(perm->mask.s_addr), perm->iport_min, perm->iport_max,
	       (perm->re)?re:"");
#endif
	return 0;
}

void
free_permission_line(struct upnpperm * perm)
{
	if(perm->re)
	{
		free(&perm->re);
		perm->re = NULL;
		regfree(&perm->regex);
	}
}

#ifdef USE_MINIUPNPDCTL
void
write_permlist(int fd, const struct upnpperm * permary,
               int nperms)
{
	int l;
	const struct upnpperm * perm;
	int i;
	char buf[128];
	write(fd, "Permissions :\n", 14);
	for(i = 0; i<nperms; i++)
	{
		perm = permary + i;
		l = snprintf(buf, sizeof(buf), "%02d %s %hu-%hu %08x/%08x %hu-%hu",
	       i,
	       (perm->type==UPNPPERM_ALLOW)?"allow":"deny",
	       perm->eport_min, perm->eport_max, ntohl(perm->address.s_addr),
	       ntohl(perm->mask.s_addr), perm->iport_min, perm->iport_max);
		if(l<0)
			return;
		write(fd, buf, l);
		if(perm->re)
		{
			write(fd, " ", 1);
			write(fd, perm->re, strlen(perm->re));
		}
		write(fd, "\n", 1);
	}
}
#endif

/* match_permission()
 * returns: 1 if eport, address, iport matches the permission rule
 *          0 if no match */
static int
match_permission(const struct upnpperm * perm,
                 u_short eport, struct in_addr address, u_short iport,
                 const char * desc)
{
	if( (eport < perm->eport_min) || (perm->eport_max < eport))
		return 0;
	if( (iport < perm->iport_min) || (perm->iport_max < iport))
		return 0;
	if( (address.s_addr & perm->mask.s_addr)
	   != (perm->address.s_addr & perm->mask.s_addr) )
		return 0;
	if(desc && perm->re && regexec(&perm->regex, desc, 0, NULL, 0) == REG_NOMATCH)
		return 0;
	return 1;
}

#if 0
/* match_permission_internal()
 * returns: 1 if address, iport matches the permission rule
 *          0 if no match */
static int
match_permission_internal(const struct upnpperm * perm,
                          struct in_addr address, u_short iport)
{
	if( (iport < perm->iport_min) || (perm->iport_max < iport))
		return 0;
	if( (address.s_addr & perm->mask.s_addr)
	   != (perm->address.s_addr & perm->mask.s_addr) )
		return 0;
	return 1;
}
#endif

int
check_upnp_rule_against_permissions(const struct upnpperm * permary,
                                    int n_perms,
                                    u_short eport, struct in_addr address,
                                    u_short iport, const char * desc)
{
	int i;
	for(i=0; i<n_perms; i++)
	{
		if(match_permission(permary + i, eport, address, iport, desc))
		{
			syslog(LOG_DEBUG,
			       "UPnP permission rule %d matched : port mapping %s",
			       i, (permary[i].type == UPNPPERM_ALLOW)?"accepted":"rejected"
			       );
			return (permary[i].type == UPNPPERM_ALLOW);
		}
	}
	syslog(LOG_DEBUG, "no permission rule matched : accept by default (n_perms=%d)", n_perms);
	return 1;	/* Default : accept */
}

void
get_permitted_ext_ports(uint32_t * allowed,
                        const struct upnpperm * permary, int n_perms,
                        in_addr_t addr, u_short iport)
{
	int i, j;

	/* build allowed external ports array */
	memset(allowed, 0xff, 65536 / 8);	/* everything allowed by default */

	for (i = n_perms - 1; i >= 0; i--)
	{
		if( (addr & permary[i].mask.s_addr)
		  != (permary[i].address.s_addr & permary[i].mask.s_addr) )
			continue;
		if( (iport < permary[i].iport_min) || (permary[i].iport_max < iport))
			continue;
		for (j = (int)permary[i].eport_min ; j <= (int)permary[i].eport_max; )
		{
			if ((j % 32) == 0 && ((int)permary[i].eport_max >= (j + 31)))
			{
				/* 32bits at once */
				allowed[j / 32] = (permary[i].type == UPNPPERM_ALLOW) ? 0xffffffff : 0;
				j += 32;
			}
			else
			{
				do
				{
					/* one bit at once */
					if (permary[i].type == UPNPPERM_ALLOW)
						allowed[j / 32] |= (1 << (j % 32));
					else
						allowed[j / 32] &= ~(1 << (j % 32));
					j++;
				}
				while ((j % 32) != 0 && (j <= (int)permary[i].eport_max));
			}
		}
	}
}
