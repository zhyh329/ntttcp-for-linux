// ----------------------------------------------------------------------------------
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
// Author: Shihua (Simon) Xiao, sixiao@microsoft.com
// ----------------------------------------------------------------------------------

#include "util.h"

void run_ntttcp_rtt_calculation_for_sender(struct ntttcp_test_endpoint *tep)
{
	uint i = 0;
	uint total_rtt = 0;
	uint num_average_rtt = 0;
	struct	ntttcp_stream_client *sc;
	uint total_test_threads = tep->total_threads;

	/* Calculate average RTT across all connections */
	for (i = 0; i < total_test_threads; i++) {
		sc = tep->client_streams[i];
		if (sc->average_rtt != (uint) -1) {
			total_rtt += sc->average_rtt;
			num_average_rtt++;
		}
	}

	if (num_average_rtt > 0)
		tep->results->average_rtt = total_rtt / num_average_rtt;
}

double get_time_diff(struct timeval *t1, struct timeval *t2)
{
	return fabs( (t1->tv_sec + (t1->tv_usec / 1000000.0)) - (t2->tv_sec + (t2->tv_usec / 1000000.0)) );
}

const long KIBI = 1<<10;
const long MEBI = 1<<20;
const long GIBI = 1<<30;
const int BYTE_TO_BITS = 8;

double unit_atod(const char *s)
{
	double n;
	char suffix = '\0';

	sscanf(s, "%lf%c", &n, &suffix);
	switch (suffix) {
	case 'g': case 'G':
		n *= GIBI;
		break;
	case 'm': case 'M':
		n *= MEBI;
		break;
	case 'k': case 'K':
		n *= KIBI;
		break;
	default:
		break;
	}
	return n;
}

const char *unit_bps[] =
{
	"bps",
	"Kbps",
	"Mbps",
	"Gbps"
};

int process_test_results(struct ntttcp_test_endpoint *tep)
{
	struct ntttcp_test_endpoint_results *tepr = tep->results;
	unsigned int i;
	double cpu_speed_mhz;
	double test_duration = tepr->actual_test_time;
	uint64_t total_bytes = tepr->total_bytes;
	long double cpu_ps_total_diff;

	if (test_duration == 0)
		return -1;

	/* calculate for per-thread counters */
	for (i=0; i<tep->total_threads; i++){
		if (tep->results->threads[i]->is_sync_thread == true)
			continue;

		tepr->threads[i]->KBps = tepr->threads[i]->total_bytes / KIBI / tepr->threads[i]->actual_test_time;
		tepr->threads[i]->MBps = tepr->threads[i]->KBps / KIBI;
		tepr->threads[i]->mbps = tepr->threads[i]->MBps * BYTE_TO_BITS;
	}

	/* calculate for overall counters */
	cpu_speed_mhz = read_value_from_proc(PROC_FILE_CPUINFO, CPU_SPEED_MHZ);
	tepr->cpu_speed_mhz = cpu_speed_mhz;

	tepr->retrans_segments_per_sec = (tepr->final_tcp_retrans->retrans_segs - tepr->init_tcp_retrans->retrans_segs) / test_duration;
	tepr->tcp_lost_retransmit_per_sec = (tepr->final_tcp_retrans->tcp_lost_retransmit - tepr->init_tcp_retrans->tcp_lost_retransmit) / test_duration;
	tepr->tcp_syn_retrans_per_sec = (tepr->final_tcp_retrans->tcp_syn_retrans - tepr->init_tcp_retrans->tcp_syn_retrans) / test_duration;
	tepr->tcp_fast_retrans_per_sec = (tepr->final_tcp_retrans->tcp_fast_retrans - tepr->init_tcp_retrans->tcp_fast_retrans) / test_duration;
	tepr->tcp_forward_retrans_per_sec = (tepr->final_tcp_retrans->tcp_forward_retrans - tepr->init_tcp_retrans->tcp_forward_retrans) / test_duration;
	tepr->tcp_slowStart_retrans_per_sec = (tepr->final_tcp_retrans->tcp_slowStart_retrans - tepr->init_tcp_retrans->tcp_slowStart_retrans) / test_duration;
	tepr->tcp_retrans_fail_per_sec = (tepr->final_tcp_retrans->tcp_retrans_fail - tepr->init_tcp_retrans->tcp_retrans_fail) / test_duration;

	tepr->packets_sent = tepr->final_tx_packets - tepr->init_tx_packets;
	tepr->packets_received = tepr->final_rx_packets - tepr->init_rx_packets;
	tepr->total_interrupts = tepr->final_interrupts - tepr->init_interrupts;
	tepr->packets_per_interrupt = tepr->total_interrupts == 0 ? 0 : (tepr->packets_sent + tepr->packets_received) / (double)tepr->total_interrupts;

	cpu_ps_total_diff = tepr->final_cpu_ps->total_time - tepr->init_cpu_ps->total_time;
	tepr->cpu_ps_user_usage = (tepr->final_cpu_ps->user_time - tepr->init_cpu_ps->user_time) / cpu_ps_total_diff;
	tepr->cpu_ps_system_usage = (tepr->final_cpu_ps->system_time - tepr->init_cpu_ps->system_time) / cpu_ps_total_diff;
	tepr->cpu_ps_idle_usage = (tepr->final_cpu_ps->idle_time - tepr->init_cpu_ps->idle_time) / cpu_ps_total_diff;
	tepr->cpu_ps_iowait_usage = (tepr->final_cpu_ps->iowait_time - tepr->init_cpu_ps->iowait_time) / cpu_ps_total_diff;
	tepr->cpu_ps_softirq_usage = (tepr->final_cpu_ps->softirq_time - tepr->init_cpu_ps->softirq_time) / cpu_ps_total_diff;

	/* calculate for counters for xml log (compatiable with Windows ntttcp.exe) */
	tepr->total_bytes_MB = total_bytes / MEBI;
	tepr->throughput_MBps = tepr->total_bytes_MB / test_duration;
	tepr->throughput_mbps = tepr->throughput_MBps * BYTE_TO_BITS;
	tepr->cycles_per_byte = total_bytes == 0 ? 0 :
			cpu_speed_mhz * 1000 * 1000 * test_duration * (tepr->final_cpu_ps->nproc) * (1 - tepr->cpu_ps_idle_usage) / total_bytes;
	tepr->packets_retransmitted = tepr->final_tcp_retrans->retrans_segs - tepr->init_tcp_retrans->retrans_segs;
	tepr->cpu_busy_percent = ((tepr->final_cpu_usage->clock - tepr->init_cpu_usage->clock) * 1000000.0 / CLOCKS_PER_SEC)
				 / (tepr->final_cpu_usage->time - tepr->init_cpu_usage->time);
	tepr->errors = 0;

	/* calculate TCP RTT */
	if (tep->endpoint_role == ROLE_SENDER && tep->test->protocol == TCP)
		run_ntttcp_rtt_calculation_for_sender(tep);

	return 0;
}

void print_test_results(struct ntttcp_test_endpoint *tep)
{
	struct ntttcp_test_endpoint_results *tepr = tep->results;
	uint64_t total_bytes = tepr->total_bytes;
	uint total_conns_created = 0;
	double test_duration = tepr->actual_test_time;

	unsigned int i;
	char *log = NULL, *log_tmp = NULL;

	if (test_duration == 0)
		return;

	if (tep->test->verbose) {
		PRINT_INFO("\tThread\tTime(s)\tThroughput");
		PRINT_INFO("\t======\t=======\t==========");
		for (i=0; i<tep->total_threads; i++) {
			if (tep->results->threads[i]->is_sync_thread == true)
				continue;

			log_tmp = format_throughput(tepr->threads[i]->total_bytes,
						    tepr->threads[i]->actual_test_time);
			ASPRINTF(&log, "\t%d\t %.2f\t %s", i, tepr->threads[i]->actual_test_time, log_tmp);
			free(log_tmp);
			PRINT_INFO_FREE(log);
		}
	}

	/* only sender/client report the total connections established */
	if (tep->test->client_role == true) {
		for (i=0; i<tep->total_threads; i++)
			total_conns_created += tep->client_streams[i]->num_conns_created;
		ASPRINTF(&log, "%d connections tested", total_conns_created);
		PRINT_INFO_FREE(log);
	}

	PRINT_INFO("#####  Totals:  #####");
	ASPRINTF(&log, "test duration\t:%.2f seconds", test_duration);
	PRINT_INFO_FREE(log);
	ASPRINTF(&log, "total bytes\t:%" PRIu64, total_bytes);
	PRINT_INFO_FREE(log);

	log_tmp = format_throughput(total_bytes, test_duration);
	ASPRINTF(&log, "\t throughput\t:%s", log_tmp);
	free(log_tmp);
	PRINT_INFO_FREE(log);

	if (tep->test->show_tcp_retransmit) {
		PRINT_INFO("tcp retransmit:");
		ASPRINTF(&log, "\t retrans_segments/sec\t:%.2f", tepr->retrans_segments_per_sec);
		PRINT_INFO_FREE(log);
		ASPRINTF(&log, "\t lost_retrans/sec\t:%.2f", tepr->tcp_lost_retransmit_per_sec);
		PRINT_INFO_FREE(log);
		ASPRINTF(&log, "\t syn_retrans/sec\t:%.2f", tepr->tcp_syn_retrans_per_sec);
		PRINT_INFO_FREE(log);
		ASPRINTF(&log, "\t fast_retrans/sec\t:%.2f", tepr->tcp_fast_retrans_per_sec);
		PRINT_INFO_FREE(log);
		ASPRINTF(&log, "\t forward_retrans/sec\t:%.2f", tepr->tcp_forward_retrans_per_sec);
		PRINT_INFO_FREE(log);
		ASPRINTF(&log, "\t slowStart_retrans/sec\t:%.2f", tepr->tcp_slowStart_retrans_per_sec);
		PRINT_INFO_FREE(log);
		ASPRINTF(&log, "\t retrans_fail/sec\t:%.2f", tepr->tcp_retrans_fail_per_sec);
		PRINT_INFO_FREE(log);
	}

	if (strcmp(tep->test->show_interface_packets, "")) {
		PRINT_INFO("total packets:");
		ASPRINTF(&log, "\t tx_packets\t:%" PRIu64, tepr->packets_sent);
		PRINT_INFO_FREE(log);
		ASPRINTF(&log, "\t rx_packets\t:%" PRIu64, tepr->packets_received);
		PRINT_INFO_FREE(log);
	}
	if (strcmp(tep->test->show_dev_interrupts, "")) {
		PRINT_INFO("interrupts:");
		ASPRINTF(&log, "\t total\t\t:%" PRIu64, tepr->total_interrupts);
		PRINT_INFO_FREE(log);
	}
	if (strcmp(tep->test->show_interface_packets, "") && strcmp(tep->test->show_dev_interrupts, "")) {
		ASPRINTF(&log, "\t pkts/interrupt\t:%.2f", tepr->packets_per_interrupt);
		PRINT_INFO_FREE(log);
	}

	if (tepr->final_cpu_ps->nproc == tepr->init_cpu_ps->nproc) {
		ASPRINTF(&log, "cpu cores\t:%d", tepr->final_cpu_ps->nproc);
		PRINT_INFO_FREE(log);
	} else {
		ASPRINTF(&log, "number of CPUs does not match: initial: %d; final: %d", tepr->init_cpu_ps->nproc, tepr->final_cpu_ps->nproc);
		PRINT_ERR_FREE(log);
	}

	ASPRINTF(&log, "\t cpu speed\t:%.3fMHz", tepr->cpu_speed_mhz);
	PRINT_INFO_FREE(log);
	ASPRINTF(&log, "\t user\t\t:%.2f%%", tepr->cpu_ps_user_usage * 100);
	PRINT_INFO_FREE(log);
	ASPRINTF(&log, "\t system\t\t:%.2f%%", tepr->cpu_ps_system_usage * 100);
	PRINT_INFO_FREE(log);
	ASPRINTF(&log, "\t idle\t\t:%.2f%%", tepr->cpu_ps_idle_usage * 100);
	PRINT_INFO_FREE(log);
	ASPRINTF(&log, "\t iowait\t\t:%.2f%%", tepr->cpu_ps_iowait_usage * 100);
	PRINT_INFO_FREE(log);
	ASPRINTF(&log, "\t softirq\t:%.2f%%", tepr->cpu_ps_softirq_usage * 100);
	PRINT_INFO_FREE(log);
	ASPRINTF(&log, "\t cycles/byte\t:%.2f", tepr->cycles_per_byte);
	PRINT_INFO_FREE(log);
	ASPRINTF(&log, "cpu busy (all)\t:%.2f%%", tepr->cpu_busy_percent * 100);
	PRINT_INFO_FREE(log);

	if (tep->test->verbose) {
		if (tep->endpoint_role == ROLE_SENDER && tep->test->protocol == TCP) {
			ASPRINTF(&log, "tcpi rtt\t\t:%u us", tepr->average_rtt);
			PRINT_INFO_FREE(log);
		}
	}
	printf("---------------------------------------------------------\n");

	if (tep->test->save_xml_log)
		if (write_result_into_log_file(tep) != 0)
			PRINT_ERR("Error writing log to xml file");
}

size_t execute_system_cmd_by_process(char *command, char *type, char *output)
{
	FILE *pfp;
	size_t count, len;

	pfp = popen(command, type);
	if (pfp == NULL) {
		PRINT_ERR("Error opening process to execute command");
		return 0;
	}

	count = getline(&output, &len, pfp);

	fclose(pfp);
	return count;
}

unsigned int escape_char_for_xml(char *in, char *out)
{
	unsigned int count = 0;
	size_t pos_in = 0, pos_out = 0;

	for(pos_in = 0; in[pos_in]; pos_in++) {
		count++;
		switch (in[pos_in]) {
		case '>':
			memcpy(out+pos_out, "&gt;", 4);
			pos_out = pos_out + 4;
			break;
		case '<':
			memcpy(out+pos_out, "&lt;", 4);
			pos_out = pos_out + 4;
			break;
		case '&':
			memcpy(out+pos_out, "&amp;", 5);
			pos_out = pos_out + 5;
			break;
		case '\'':
			memcpy(out+pos_out, "&apos;", 6);
			pos_out = pos_out + 6;
			break;
		case '\"':
			memcpy(out+pos_out, "&quot;", 6);
			pos_out = pos_out + 6;
			break;
		case '\n':
			break;
		default:
			count--;
			memcpy(out+pos_out, in+pos_in, 1);
			pos_out++;
		}
	}
	return count;
}

int write_result_into_log_file(struct ntttcp_test_endpoint *tep)
{
	struct ntttcp_test *test = tep->test;
	struct ntttcp_test_endpoint_results *tepr = tep->results;
	char str_temp1[256];
	char str_temp2[2048];
	size_t count = 0;
	unsigned int i;

	memset(str_temp1, '\0', sizeof(char) * 256);
	memset(str_temp2, '\0', sizeof(char) * 2048);

	FILE *logfile = fopen(test->xml_log_filename, "w");
	if (logfile == NULL) {
		PRINT_ERR("Error opening file to write log");
		return -1;
	}

	gethostname(str_temp1, 256);
	fprintf(logfile, "<ntttcp%s computername=\"%s\" version=\"5.33-linux\">\n", tep->endpoint_role == ROLE_RECEIVER ? "r" : "s", str_temp1);
	fprintf(logfile, "	<parameters>\n");
	fprintf(logfile, "		<send_socket_buff>%lu</send_socket_buff>\n", test->send_buf_size);
	fprintf(logfile, "		<recv_socket_buff>%lu</recv_socket_buff>\n", test->recv_buf_size);
	fprintf(logfile, "		<port>%d</port>\n", test->server_base_port);
	fprintf(logfile, "		<sync_port>%s</sync_port>\n", "False");
	fprintf(logfile, "		<no_sync>%s</no_sync>\n", "False");
	fprintf(logfile, "		<wait_timeout_milliseconds>%d</wait_timeout_milliseconds>\n", 0);
	fprintf(logfile, "		<async>%s</async>\n", "False");
	fprintf(logfile, "		<verbose>%s</verbose>\n", test->verbose ? "True":"False");
	fprintf(logfile, "		<wsa>%s</wsa>\n", "False");
	fprintf(logfile, "		<use_ipv6>%s</use_ipv6>\n", test->domain == AF_INET6 ? "True":"False");
	fprintf(logfile, "		<udp>%s</udp>\n", test->protocol == UDP? "True":"False");
	fprintf(logfile, "		<verify_data>%s</verify_data>\n", "False");
	fprintf(logfile, "		<wait_all>%s</wait_all>\n", "False");
	fprintf(logfile, "		<run_time>%d</run_time>\n", test->duration);
	fprintf(logfile, "		<warmup_time>%d</warmup_time>\n", 0);
	fprintf(logfile, "		<cooldown_time>%d</cooldown_time>\n", 0);
	fprintf(logfile, "		<dash_n_timeout>%d</dash_n_timeout>\n", 0);
	fprintf(logfile, "		<bind_sender>%s</bind_sender>\n", "False");
	fprintf(logfile, "		<sender_name>%s</sender_name>\n", "NA");
	fprintf(logfile, "		<max_active_threads>%d</max_active_threads>\n", 0);
	fprintf(logfile, "		<tp>%s</tp>\n", "False");
	fprintf(logfile, "		<no_stdio_buffer>%s</no_stdio_buffer>\n", "False");
	fprintf(logfile, "		<throughput_Bpms>%d</throughput_Bpms>\n", 0);
	fprintf(logfile, "		<cpu_burn>%d</cpu_burn>\n", 0);
	fprintf(logfile, "		<latency_measurement>%s</latency_measurement>\n", "False");
	fprintf(logfile, "		<use_io_compl_ports>%s</use_io_compl_ports>\n", "NA");
	fprintf(logfile, "		<cpu_from_idle_flag>%s</cpu_from_idle_flag>\n", "False");
	fprintf(logfile, "		<get_estats>%s</get_estats>\n", "False");
	fprintf(logfile, "		<qos_flag>%s</qos_flag>\n", "False");
	fprintf(logfile, "		<jitter_measurement>%s</jitter_measurement>\n", "False");
	fprintf(logfile, "		<packet_spacing>%d</packet_spacing>\n", 0);
	fprintf(logfile, "	</parameters>\n");

	for(i = 0; i < tep->total_threads; i++ ){
		if (tep->results->threads[i]->is_sync_thread == true)
			continue;

		fprintf(logfile, "	<thread index=\"%i\">\n", i);
		fprintf(logfile, "		<realtime metric=\"s\">%.3f</realtime>\n", tepr->threads[i]->actual_test_time);
		fprintf(logfile, "		<throughput metric=\"KB/s\">%.3f</throughput>\n", tepr->threads[i]->KBps);
		fprintf(logfile, "		<throughput metric=\"MB/s\">%.3f</throughput>\n", tepr->threads[i]->MBps);
		fprintf(logfile, "		<throughput metric=\"mbps\">%.3f</throughput>\n", tepr->threads[i]->mbps);
		fprintf(logfile, "		<avg_bytes_per_compl metric=\"B\">%.3f</avg_bytes_per_compl>\n", 0.000);
		fprintf(logfile, "	</thread>\n");
	}

	fprintf(logfile, "	<total_bytes metric=\"MB\">%.6f</total_bytes>\n", tepr->total_bytes_MB);
	fprintf(logfile, "	<realtime metric=\"s\">%.6f</realtime>\n", tepr->actual_test_time);
	fprintf(logfile, "	<avg_bytes_per_compl metric=\"B\">%.3f</avg_bytes_per_compl>\n", 0.000);
	fprintf(logfile, "	<threads_avg_bytes_per_compl metric=\"B\">%.3f</threads_avg_bytes_per_compl>\n", 0.000);
	fprintf(logfile, "	<avg_frame_size metric=\"B\">%.3f</avg_frame_size>\n", 0.000);
	fprintf(logfile, "	<throughput metric=\"MB/s\">%.3f</throughput>\n", tepr->throughput_MBps);
	fprintf(logfile, "	<throughput metric=\"mbps\">%.3f</throughput>\n", tepr->throughput_mbps);
	fprintf(logfile, "	<total_buffers>%.3f</total_buffers>\n", 0.000);
	fprintf(logfile, "	<throughput metric=\"buffers/s\">%.3f</throughput>\n", 0.000);
	fprintf(logfile, "	<avg_packets_per_interrupt metric=\"packets/interrupt\">%.3f</avg_packets_per_interrupt>\n", tepr->packets_per_interrupt);
	fprintf(logfile, "	<interrupts metric=\"count/sec\">%.3f</interrupts>\n", 0.000);
	fprintf(logfile, "	<dpcs metric=\"count/sec\">%.3f</dpcs>\n", 0.000);
	fprintf(logfile, "	<avg_packets_per_dpc metric=\"packets/dpc\">%.3f</avg_packets_per_dpc>\n", 0.000);
	fprintf(logfile, "	<cycles metric=\"cycles/byte\">%.3f</cycles>\n", tepr->cycles_per_byte);
	fprintf(logfile, "	<packets_sent>%lu</packets_sent>\n", tepr->packets_sent);
	fprintf(logfile, "	<packets_received>%lu</packets_received>\n", tepr->packets_received);
	fprintf(logfile, "	<packets_retransmitted>%lu</packets_retransmitted>\n", tepr->packets_retransmitted);
	fprintf(logfile, "	<errors>%d</errors>\n", tepr->errors);
	fprintf(logfile, "	<cpu metric=\"%%\">%.3f</cpu>\n", tepr->cpu_busy_percent * 100);
	fprintf(logfile, "	<bufferCount>%u</bufferCount>\n", 0);
	fprintf(logfile, "	<bufferLen>%u</bufferLen>\n", 0);
	fprintf(logfile, "	<io>%u</io>\n", 0);

	if (tep->endpoint_role == ROLE_SENDER && test->protocol == TCP) {
		fprintf(logfile, "	<tcp_average_rtt metric=\"us\">%u</tcp_average_rtt>\n", tepr->average_rtt);
	}

	count = execute_system_cmd_by_process("uname -a", "r", str_temp1);
	escape_char_for_xml(str_temp1, str_temp2);
	fprintf(logfile, "	<os>%s</os>\n", count == 0 ? "Unknown" : str_temp2);

	fprintf(logfile, "</ntttcp%s>\n", tep->endpoint_role == ROLE_RECEIVER ? "r": "s");

	fclose(logfile);
	return 0;
}

char *format_throughput(uint64_t bytes_transferred, double test_duration)
{
	double tmp = 0;
	int unit_idx = 0;
	char *throughput;

	tmp = bytes_transferred * 8.0 / test_duration;
	while (tmp > 1000 && unit_idx < 3) {
		tmp /= 1000.0;
		unit_idx++;
	}

	ASPRINTF(&throughput, "%.2f%s", tmp, unit_bps[unit_idx]);
	return throughput;
}

char *retrive_ip_address_str(struct sockaddr_storage *ss, char *ip_str, size_t maxlen)
{
	switch(ss->ss_family) {
	case AF_INET:
		inet_ntop(AF_INET, &(((struct sockaddr_in *)ss)->sin_addr), ip_str, maxlen);
		break;

	case AF_INET6:
		inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)ss)->sin6_addr), ip_str, maxlen);
		break;

	default:
		break;
	}
	return ip_str;
}

char *retrive_ip4_address_str(struct sockaddr_in *ss, char *ip_str, size_t maxlen)
{
	inet_ntop(AF_INET, &(ss->sin_addr), ip_str, maxlen);
	return ip_str;
}

char *retrive_ip6_address_str(struct sockaddr_in6 *ss, char *ip_str, size_t maxlen)
{
	inet_ntop(AF_INET6, &(ss->sin6_addr), ip_str, maxlen);
	return ip_str;
}

int set_socket_non_blocking(int fd)
{
	int flags, rtn;
	flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1)
		return -1;

	flags |= O_NONBLOCK;
	rtn = fcntl(fd, F_SETFL, flags);
	if (rtn == -1)
		return -1;

	return 0;
}

bool check_resource_limit(struct ntttcp_test *test)
{
	char *log;
	unsigned long soft_limit = 0;
	unsigned long hard_limit = 0;
	uint total_connections = 0;
	bool verbose_log = test->verbose;

	struct rlimit limitstruct;
	if(-1 == getrlimit(RLIMIT_NOFILE, &limitstruct))
		PRINT_ERR("Failed to load resource limits");

	soft_limit = (unsigned long)limitstruct.rlim_cur;
	hard_limit = (unsigned long)limitstruct.rlim_max;

	ASPRINTF(&log, "user limits for maximum number of open files: soft: %ld; hard: %ld",
			soft_limit,
			hard_limit);
	PRINT_DBG_FREE(log);

	if (test->client_role == true) {
		total_connections = test->server_ports * test->threads_per_server_port * test->conns_per_thread;
	} else {
		/*
		 * for receiver, just do a minial check;
		 * because we don't know how many threads_per_server_port will be used by sender.
		 */
		total_connections = test->server_ports * 1;
	}

	if (total_connections > soft_limit) {
		ASPRINTF(&log, "soft limit is too small: limit is %ld; but total connections will be %d (or more on receiver)",
				soft_limit,
				total_connections);
		PRINT_ERR_FREE(log);

		return false;
	} else {
		return true;
	}
}

