
/***************************************************************************
 * targets.cc -- Functions relating to "ping scanning" as well as          *
 * determining the exact IPs to hit based on CIDR and other input          *
 * formats.                                                                *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1996-2009 Insecure.Com LLC. Nmap is    *
 * also a registered trademark of Insecure.Com LLC.  This program is free  *
 * software; you may redistribute and/or modify it under the terms of the  *
 * GNU General Public License as published by the Free Software            *
 * Foundation; Version 2 with the clarifications and exceptions described  *
 * below.  This guarantees your right to use, modify, and redistribute     *
 * this software under certain conditions.  If you wish to embed Nmap      *
 * technology into proprietary software, we sell alternative licenses      *
 * (contact sales@insecure.com).  Dozens of software vendors already       *
 * license Nmap technology such as host discovery, port scanning, OS       *
 * detection, and version detection.                                       *
 *                                                                         *
 * Note that the GPL places important restrictions on "derived works", yet *
 * it does not provide a detailed definition of that term.  To avoid       *
 * misunderstandings, we consider an application to constitute a           *
 * "derivative work" for the purpose of this license if it does any of the *
 * following:                                                              *
 * o Integrates source code from Nmap                                      *
 * o Reads or includes Nmap copyrighted data files, such as                *
 *   nmap-os-db or nmap-service-probes.                                    *
 * o Executes Nmap and parses the results (as opposed to typical shell or  *
 *   execution-menu apps, which simply display raw Nmap output and so are  *
 *   not derivative works.)                                                * 
 * o Integrates/includes/aggregates Nmap into a proprietary executable     *
 *   installer, such as those produced by InstallShield.                   *
 * o Links to a library or executes a program that does any of the above   *
 *                                                                         *
 * The term "Nmap" should be taken to also include any portions or derived *
 * works of Nmap.  This list is not exclusive, but is meant to clarify our *
 * interpretation of derived works with some common examples.  Our         *
 * interpretation applies only to Nmap--we don't speak for other people's  *
 * GPL works.                                                              *
 *                                                                         *
 * If you have any questions about the GPL licensing restrictions on using *
 * Nmap in non-GPL works, we would be happy to help.  As mentioned above,  *
 * we also offer alternative license to integrate Nmap into proprietary    *
 * applications and appliances.  These contracts have been sold to dozens  *
 * of software vendors, and generally include a perpetual license as well  *
 * as providing for priority support and updates as well as helping to     *
 * fund the continued development of Nmap technology.  Please email        *
 * sales@insecure.com for further information.                             *
 *                                                                         *
 * As a special exception to the GPL terms, Insecure.Com LLC grants        *
 * permission to link the code of this program with any version of the     *
 * OpenSSL library which is distributed under a license identical to that  *
 * listed in the included COPYING.OpenSSL file, and distribute linked      *
 * combinations including the two. You must obey the GNU GPL in all        *
 * respects for all of the code used other than OpenSSL.  If you modify    *
 * this file, you may extend this exception to your version of the file,   *
 * but you are not obligated to do so.                                     *
 *                                                                         *
 * If you received these files with a written license agreement or         *
 * contract stating terms other than the terms above, then that            *
 * alternative license agreement takes precedence over these comments.     *
 *                                                                         *
 * Source is provided to this software because we believe users have a     *
 * right to know exactly what a program is going to do before they run it. *
 * This also allows you to audit the software for security holes (none     *
 * have been found so far).                                                *
 *                                                                         *
 * Source code also allows you to port Nmap to new platforms, fix bugs,    *
 * and add new features.  You are highly encouraged to send your changes   *
 * to nmap-dev@insecure.org for possible incorporation into the main       *
 * distribution.  By sending these changes to Fyodor or one of the         *
* Insecure.Org development mailing lists, it is assumed that you are      *
* offering the Nmap Project (Insecure.Com LLC) the unlimited,             *
* non-exclusive right to reuse, modify, and relicense the code.  Nmap     *
* will always be available Open Source, but this is important because the *
* inability to relicense code has caused devastating problems for other   *
* Free Software projects (such as KDE and NASM).  We also occasionally    *
* relicense the code to third parties as discussed above.  If you wish to *
* specify special license conditions of your contributions, just say so   *
* when you send them.                                                     *
*                                                                         *
* This program is distributed in the hope that it will be useful, but     *
* WITHOUT ANY WARRANTY; without even the implied warranty of              *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
* General Public License v2.0 for more details at                         *
* http://www.gnu.org/licenses/gpl-2.0.html , or in the COPYING file       *
* included with Nmap.                                                     *
*                                                                         *
***************************************************************************/

/* $Id: targets.cc 12955 2009-04-15 00:37:03Z fyodor $ */


#include "ncrack.h"
#include "targets.h"
#include "TargetGroup.h"
#include "Target.h"
#include "NcrackOps.h"
#include "utils.h"

extern NcrackOps o;
using namespace std;

/* Gets the host number (index) of target in the hostbatch array of
	 pointers.  Note that the target MUST EXIST in the array or all
	 heck will break loose. */
static inline int gethostnum(Target *hostbatch[], Target *target) {
	int i = 0;
	do {
		if (hostbatch[i] == target)
			return i;
	} while(++i);

	fatal("fluxx0red");
	return 0; // Unreached
}



/* Returns the last host obtained by nexthost.  It will be given again the next
	 time you call nexthost(). */
void returnhost(HostGroupState *hs) {
	assert(hs->next_batch_no > 0);
	hs->next_batch_no--;
}

/* Is the host passed as Target to be excluded, much of this logic had  (mdmcl)
 * to be rewritten from wam's original code to allow for the objects */
static int hostInExclude(struct sockaddr *checksock, size_t checksocklen, 
		TargetGroup *exclude_group) {
	unsigned long tmpTarget; /* ip we examine */
	int i=0;                 /* a simple index */
	char targets_type;       /* what is the address type of the Target Group */
	struct sockaddr_storage ss; 
	struct sockaddr_in *sin = (struct sockaddr_in *) &ss;
	size_t slen;             /* needed for funct but not used */
	unsigned long mask = 0;  /* our trusty netmask, which we convert to nbo */
	struct sockaddr_in *checkhost;

	if ((TargetGroup *)0 == exclude_group)
		return 0;

	assert(checksocklen >= sizeof(struct sockaddr_in));
	checkhost = (struct sockaddr_in *) checksock;
	if (checkhost->sin_family != AF_INET)
		checkhost = NULL;

	/* First find out what type of addresses are in the target group */
	targets_type = exclude_group[i].get_targets_type();

	/* Lets go through the targets until we reach our uninitialized placeholder */
	while (exclude_group[i].get_targets_type() != TargetGroup::TYPE_NONE)
	{ 
		/* while there are still hosts in the target group */
		while (exclude_group[i].get_next_host(&ss, &slen) == 0) {
			tmpTarget = sin->sin_addr.s_addr; 

			/* For Netmasks simply compare the network bits and move to the next
			 * group if it does not compare, we don't care about the individual addrs */
			if (targets_type == TargetGroup::IPV4_NETMASK) {
				mask = htonl((unsigned long) (0-1) << (32-exclude_group[i].get_mask()));
				if ((tmpTarget & mask) == (checkhost->sin_addr.s_addr & mask)) {
					exclude_group[i].rewind();
					return 1;
				}
				else {
					break;
				}
			} 
			/* For ranges we need to be a little more slick, if we don't find a match
			 * we should skip the rest of the addrs in the octet, thank wam for this
			 * optimization */
			else if (targets_type == TargetGroup::IPV4_RANGES) {
				if (tmpTarget == checkhost->sin_addr.s_addr) {
					exclude_group[i].rewind();
					return 1;
				}
				else { /* note these are in network byte order */
					if ((tmpTarget & 0x000000ff) != (checkhost->sin_addr.s_addr & 0x000000ff))
						exclude_group[i].skip_range(TargetGroup::FIRST_OCTET); 
					else if ((tmpTarget & 0x0000ff00) != (checkhost->sin_addr.s_addr & 0x0000ff00))
						exclude_group[i].skip_range(TargetGroup::SECOND_OCTET); 
					else if ((tmpTarget & 0x00ff0000) != (checkhost->sin_addr.s_addr & 0x00ff0000))
						exclude_group[i].skip_range(TargetGroup::THIRD_OCTET); 

					continue;
				}
			}
#if HAVE_IPV6
			else if (targets_type == TargetGroup::IPV6_ADDRESS) {
				fatal("exclude file not supported for IPV6 -- If it is important to you, send a mail to fyodor@insecure.org so I can guage support\n");
			}
#endif
		}
		exclude_group[i++].rewind();
	}

	/* we did not find the host */
	return 0;
}

/* loads an exclude file into an exclude target list  (mdmcl) */
TargetGroup* load_exclude(FILE *fExclude, char *szExclude) {
	int i=0;			/* loop counter */
	int iLine=0;			/* line count */
	int iListSz=0;		/* size of our exclude target list. 
										 * It doubles in size as it gets
										 *  close to filling up
										 */
	char acBuf[512];
	char *p_acBuf;
	TargetGroup *excludelist;	/* list of ptrs to excluded targets */
	char *pc;			/* the split out exclude expressions */
	char b_file = (char)0;        /* flag to indicate if we are using a file */

	/* If there are no params return now with a NULL list */
	if (((FILE *)0 == fExclude) && ((char *)0 == szExclude)) {
		excludelist=NULL;
		return excludelist;
	}

	if ((FILE *)0 != fExclude)
		b_file = (char)1;

	/* Since I don't know of a realloc equiv in C++, we will just count
	 * the number of elements here. */

	/* If the input was given to us in a file, count the number of elements
	 * in the file, and reset the file */
	if (1 == b_file) {
		while ((char *)0 != fgets(acBuf,sizeof(acBuf), fExclude)) {
			/* the last line can contain no newline, then we have to check for EOF */
			if ((char *)0 == strchr(acBuf, '\n') && !feof(fExclude)) {
				fatal("Exclude file line %d was too long to read.  Exiting.", iLine);
			}
			pc=strtok(acBuf, "\t\n ");	
			while (NULL != pc) {
				iListSz++;
				pc=strtok(NULL, "\t\n ");
			}
		}
		rewind(fExclude);
	} /* If the exclude file was provided via command line, count the elements here */
	else {
		p_acBuf=strdup(szExclude);
		pc=strtok(p_acBuf, ",");
		while (NULL != pc) {
			iListSz++;
			pc=strtok(NULL, ",");
		}
		free(p_acBuf);
		p_acBuf = NULL;
	}

	/* allocate enough TargetGroups to cover our entries, plus one that
	 * remains uninitialized so we know we reached the end */
	excludelist = new TargetGroup[iListSz + 1];

	/* don't use a for loop since the counter isn't incremented if the 
	 * exclude entry isn't parsed
	 */
	i=0;
	if (1 == b_file) {
		/* If we are parsing a file load the exclude list from that */
		while ((char *)0 != fgets(acBuf, sizeof(acBuf), fExclude)) {
			++iLine;
			if ((char *)0 == strchr(acBuf, '\n') && !feof(fExclude)) {
				fatal("Exclude file line %d was too long to read.  Exiting.", iLine);
			}

			pc=strtok(acBuf, "\t\n ");	

			while ((char *)0 != pc) {
				if(excludelist[i].parse_expr(pc,o.af()) == 0) {
					if (o.debugging > 1)
						error("Loaded exclude target of: %s", pc);
					++i;
				} 
				pc=strtok(NULL, "\t\n ");
			}
		}
	}
	else {
		/* If we are parsing command line, load the exclude file from the string */
		p_acBuf=strdup(szExclude);
		pc=strtok(p_acBuf, ",");

		while (NULL != pc) {
			if(excludelist[i].parse_expr(pc,o.af()) == 0) {
				if (o.debugging >1)
					error("Loaded exclude target of: %s", pc);
				++i;
			} 

			/* This is a totally cheezy hack, but since I can't use strtok_r...
			 * If you can think of a better way to do this, feel free to change.
			 * As for now, we will reset strtok each time we leave parse_expr */
			{
				int hack_i;
				char *hack_c = strdup(szExclude);

				pc=strtok(hack_c, ",");

				for (hack_i = 0; hack_i < i; hack_i++) 
					pc=strtok(NULL, ",");

				free(hack_c);
			}
		} 
	}
	return excludelist;
}

/* A debug routine to dump some information to stdout.                  (mdmcl)
 * Invoked if debugging is set to 3 or higher
 * I had to make signigicant changes from wam's code. Although wam
 * displayed much more detail, alot of this is now hidden inside
 * of the Target Group Object. Rather than writing a bunch of methods
 * to return private attributes, which would only be used for 
 * debugging, I went for the method below.
 */
int dumpExclude(TargetGroup *exclude_group) {
	int i=0, debug_save=0, type=TargetGroup::TYPE_NONE;
	unsigned int mask = 0;
	struct sockaddr_storage ss;
	struct sockaddr_in *sin = (struct sockaddr_in *) &ss;
	size_t slen;

	/* shut off debugging for now, this is a debug routine in itself,
	 * we don't want to see all the debug messages inside of the object */
	debug_save = o.debugging;
	o.debugging = 0;

	while ((type = exclude_group[i].get_targets_type()) != TargetGroup::TYPE_NONE)
	{
		switch (type) {
			case TargetGroup::IPV4_NETMASK:
				exclude_group[i].get_next_host(&ss, &slen);
				mask = exclude_group[i].get_mask();
				error("exclude host group %d is %s/%d\n", i, inet_ntoa(sin->sin_addr), mask);
				break;

			case TargetGroup::IPV4_RANGES:
				while (exclude_group[i].get_next_host(&ss, &slen) == 0) 
					error("exclude host group %d is %s\n", i, inet_ntoa(sin->sin_addr));
				break;

			case TargetGroup::IPV6_ADDRESS:
				fatal("IPV6 addresses are not supported in the exclude file\n");
				break;

			default:
				fatal("Unknown target type in exclude file.\n");
		}
		exclude_group[i++].rewind();
	}

	/* return debugging to what it was */
	o.debugging = debug_save; 
	return 1;
}


Target *nexthost(HostGroupState *hs, TargetGroup *exclude_group,
		struct scan_lists *ports, int pingtype) {
	int hidx = 0;
	int i;
	struct sockaddr_storage ss;
	size_t sslen;
	uint32_t ifbuf[200] ;
	struct timeval now;


	if (hs->next_batch_no < hs->current_batch_sz) {
		/* Woop!  This is easy -- we just pass back the next host struct */
		return hs->hostbatch[hs->next_batch_no++];
	}

	hs->current_batch_sz = hs->next_batch_no = 0;
	do {
		/* Grab anything we have in our current_expression */
		while (hs->current_batch_sz < hs->max_batch_sz && 
				hs->current_expression.get_next_host(&ss, &sslen) == 0)
		{
			if (hostInExclude((struct sockaddr *)&ss, sslen, exclude_group)) {
				continue; /* Skip any hosts the user asked to exclude */
			}
			hidx = hs->current_batch_sz;
			hs->hostbatch[hidx] = new Target();
			hs->hostbatch[hidx]->setTargetSockAddr(&ss, sslen);

			/* put target expression in target if we have a named host without netmask */
			if ( hs->current_expression.get_targets_type() == TargetGroup::IPV4_NETMASK  &&
					hs->current_expression.get_namedhost() &&
					!strchr( hs->target_expressions[hs->next_expression-1], '/' ) ) {
				hs->hostbatch[hidx]->setTargetName(hs->target_expressions[hs->next_expression-1]);
			}
			hs->current_batch_sz++;
			o.numhosts_scanned++;
		}

		if (hs->current_batch_sz < hs->max_batch_sz &&
				hs->next_expression < hs->num_expressions) {
			/* We are going to have to pop in another expression. */
			while(hs->current_expression.parse_expr(hs->target_expressions[hs->next_expression++], o.af()) != 0) 
				if (hs->next_expression >= hs->num_expressions)
					break;
		} else break;
	} while(1);

batchfull:

	if (hs->current_batch_sz == 0)
		return NULL;


	return hs->hostbatch[hs->next_batch_no++];
}
