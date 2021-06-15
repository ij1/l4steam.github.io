# Extracted from l4sdemo and adapted to this testbed

start_dyn_server() {
	local ns=$1
	local serie=$2
	local port=$3
	WGLS="$WLGS run_httpserver"

	echo "[$ns] ${PATH_TO_TRAFFIC_GENERATOR}/http_server/run_httpserver $port ${PATH_TO_TRAFFIC_GENERATOR}/gen_rsample/rs${serie}.txt"
	ns_exec_silent "$ns" "${PATH_TO_TRAFFIC_GENERATOR}/http_server/run_httpserver" \
		"$port" "${PATH_TO_TRAFFIC_GENERATOR}/gen_rsample/rs${serie}.txt" \
		&> "${DATA_DIR}/httpserver_$(gen_suffix $ns)" &
}

start_dyn_client() {
	local ns=$1
	local serie=$2
	local key="$3-e0"
	local dst="${IPADDR[$key]}"
	local port=$4
	local mit=$5
	local link=$(echo "$RATE" | sed -e 's|[^0-9]||g')
	WGLS="$WLGS http_clients_itime"

	echo "[$ns] ${PATH_TO_TRAFFIC_GENERATOR}/http_client/http_clients_itime $dst $port ${PATH_TO_TRAFFIC_GENERATOR}/gen_rsample/rit${mit}_${serie}.txt ${link}"
	ns_exec_silent "$ns" "${PATH_TO_TRAFFIC_GENERATOR}/http_client/http_clients_itime" \
		"$dst" "$port" "${PATH_TO_TRAFFIC_GENERATOR}/gen_rsample/rit${mit}_${serie}.txt" \
		"${link}" \
		&> "${DATA_DIR}/httpclient_$(gen_suffix $ns)" &
}

start_static_server() {
	local ns=$1
	WGLS="$WLGS dl_server"

	echo "[$ns] ${PATH_TO_TRAFFIC_GENERATOR}/dl_server/dl_server 5555"
	ns_exec_silent "$ns" "${PATH_TO_TRAFFIC_GENERATOR}/dl_server/dl_server" 5555 \
		&> "${DATA_DIR}/dlserver_$(gen_suffix $ns)" &
}	

start_static_client() {
	local ns=$1
	local key="$2-e0"
        local dst="${IPADDR[$key]}"
	WGLS="$WLGS dl_client"

	echo "[$ns] ${PATH_TO_TRAFFIC_GENERATOR}/dl_client/dl_client $dst 5555 1"
	ns_exec_silent "$ns" "${PATH_TO_TRAFFIC_GENERATOR}/dl_client/dl_client" "$dst" 5555 1 \
		&> "${DATA_DIR}/dlclient_$(gen_suffix $ns)" &
}
