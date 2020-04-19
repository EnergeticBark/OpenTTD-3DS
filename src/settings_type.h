/* $Id$ */

/** @file settings_type.h Types related to global configuration settings. */

#ifndef SETTINGS_TYPE_H
#define SETTINGS_TYPE_H

#include "date_type.h"
#include "town_type.h"
#include "transport_type.h"
#include "network/core/config.h"
#include "company_type.h"

/** Settings related to the difficulty of the game */
struct DifficultySettings {
	byte   max_no_competitors;               ///< the number of competitors (AIs)
	byte   number_towns;                     ///< the amount of towns
	byte   number_industries;                ///< the amount of industries
	uint32 max_loan;                         ///< the maximum initial loan
	byte   initial_interest;                 ///< amount of interest (to pay over the loan)
	byte   vehicle_costs;                    ///< amount of money spent on vehicle running cost
	byte   competitor_speed;                 ///< the speed at which the AI builds
	byte   vehicle_breakdowns;               ///< likelihood of vehicles breaking down
	byte   subsidy_multiplier;               ///< amount of subsidy
	byte   construction_cost;                ///< how expensive is building
	byte   terrain_type;                     ///< the mountainousness of the landscape
	byte   quantity_sea_lakes;               ///< the amount of seas/lakes
	byte   economy;                          ///< how volatile is the economy
	byte   line_reverse_mode;                ///< reversing at stations or not
	byte   disasters;                        ///< are disasters enabled
	byte   town_council_tolerance;           ///< minimum required town ratings to be allowed to demolish stuff
	byte   diff_level;                       ///< the difficulty level
};

/** Settings related to the GUI and other stuff that is not saved in the savegame. */
struct GUISettings {
	bool   vehicle_speed;                    ///< show vehicle speed
	bool   sg_full_load_any;                 ///< new full load calculation, any cargo must be full read from pre v93 savegames
	bool   lost_train_warn;                  ///< if a train can't find its destination, show a warning
	uint8  order_review_system;              ///< perform order reviews on vehicles
	bool   vehicle_income_warn;              ///< if a vehicle isn't generating income, show a warning
	bool   status_long_date;                 ///< always show long date in status bar
	bool   show_finances;                    ///< show finances at end of year
	bool   sg_new_nonstop;                   ///< ttdpatch compatible nonstop handling read from pre v93 savegames
	bool   new_nonstop;                      ///< ttdpatch compatible nonstop handling
	bool   autoscroll;                       ///< scroll when moving mouse to the edge
	byte   errmsg_duration;                  ///< duration of error message
	bool   link_terraform_toolbar;           ///< display terraform toolbar when displaying rail, road, water and airport toolbars
	bool   reverse_scroll;                   ///< right-Click-Scrolling scrolls in the opposite direction
	bool   smooth_scroll;                    ///< smooth scroll viewports
	bool   measure_tooltip;                  ///< show a permanent tooltip when dragging tools
	byte   liveries;                         ///< options for displaying company liveries, 0=none, 1=self, 2=all
	bool   prefer_teamchat;                  ///< choose the chat message target with <ENTER>, true=all clients, false=your team
	uint8  advanced_vehicle_list;            ///< use the "advanced" vehicle list
	uint8  loading_indicators;               ///< show loading indicators
	uint8  default_rail_type;                ///< the default rail type for the rail GUI
	uint8  toolbar_pos;                      ///< position of toolbars, 0=left, 1=center, 2=right
	uint8  window_snap_radius;               ///< windows snap at each other if closer than this
	uint8  window_soft_limit;                ///< soft limit of maximum number of non-stickied non-vital windows (0 = no limit)
	bool   always_build_infrastructure;      ///< always allow building of infrastructure, even when you do not have the vehicles for it
	byte   autosave;                         ///< how often should we do autosaves?
	bool   keep_all_autosave;                ///< name the autosave in a different way
	bool   autosave_on_exit;                 ///< save an autosave when you quit the game, but do not ask "Do you really want to quit?"
	uint8  date_format_in_default_names;     ///< should the default savegame/screenshot name use long dates (31th Dec 2008), short dates (31-12-2008) or ISO dates (2008-12-31)
	byte   max_num_autosaves;                ///< controls how many autosavegames are made before the game starts to overwrite (names them 0 to max_num_autosaves - 1)
	bool   population_in_label;              ///< show the population of a town in his label?
	uint8  right_mouse_btn_emulation;        ///< should we emulate right mouse clicking?
	uint8  scrollwheel_scrolling;            ///< scrolling using the scroll wheel?
	uint8  scrollwheel_multiplier;           ///< how much 'wheel' per incoming event from the OS?
	bool   left_mouse_btn_scrolling;         ///< left mouse button scroll
	bool   pause_on_newgame;                 ///< whether to start new games paused or not
	bool   enable_signal_gui;                ///< show the signal GUI when the signal button is pressed
	Year   coloured_news_year;               ///< when does newspaper become coloured?
	bool   timetable_in_ticks;               ///< whether to show the timetable in ticks rather than days
	bool   quick_goto;                       ///< Allow quick access to 'goto button' in vehicle orders window
	bool   bridge_pillars;                   ///< show bridge pillars for high bridges
	bool   auto_euro;                        ///< automatically switch to euro in 2002
	byte   drag_signals_density;             ///< many signals density
	Year   semaphore_build_before;           ///< build semaphore signals automatically before this year
	bool   autorenew;                        ///< should autorenew be enabled for new companies?
	int16  autorenew_months;                 ///< how many months from EOL of vehicles should autorenew trigger for new companies?
	int32  autorenew_money;                  ///< how much money before autorenewing for new companies?
	byte   news_message_timeout;             ///< how much longer than the news message "age" should we keep the message in the history
	bool   show_track_reservation;           ///< highlight reserved tracks.
	uint8  default_signal_type;              ///< the signal type to build by default.
	uint8  cycle_signal_types;               ///< what signal types to cycle with the build signal tool.
	byte   station_numtracks;                ///< the number of platforms to default on for rail stations
	byte   station_platlength;               ///< the platform length, in tiles, for rail stations
	bool   station_dragdrop;                 ///< whether drag and drop is enabled for stations
	bool   station_show_coverage;            ///< whether to highlight coverage area
	bool   persistent_buildingtools;         ///< keep the building tools active after usage
	uint8  expenses_layout;                  ///< layout of expenses window

	uint16 console_backlog_timeout;          ///< the minimum amount of time items should be in the console backlog before they will be removed in ~3 seconds granularity.
	uint16 console_backlog_length;           ///< the minimum amount of items in the console backlog before items will be removed.
#ifdef ENABLE_NETWORK
	uint16 network_chat_box_width;           ///< width of the chat box in pixels
	uint8  network_chat_box_height;          ///< height of the chat box in lines
#endif
};

/** Settings related to currency/unit systems. */
struct LocaleSettings {
	byte   currency;                         ///< currency we currently use
	byte   units;                            ///< unit system we show everything
};

/** All settings related to the network. */
struct NetworkSettings {
#ifdef ENABLE_NETWORK
	uint16 sync_freq;                                     ///< how often do we check whether we are still in-sync
	uint8  frame_freq;                                    ///< how often do we send commands to the clients
	uint16 max_join_time;                                 ///< maximum amount of time, in game ticks, a client may take to join
	bool   pause_on_join;                                 ///< pause the game when people join
	char   server_bind_ip[NETWORK_HOSTNAME_LENGTH];       ///< IP address the server binds to
	uint16 server_port;                                   ///< port the server listens on
	char   server_name[NETWORK_NAME_LENGTH];              ///< name of the server
	char   server_password[NETWORK_PASSWORD_LENGTH];      ///< passowrd for joining this server
	char   rcon_password[NETWORK_PASSWORD_LENGTH];        ///< passowrd for rconsole (server side)
	bool   server_advertise;                              ///< advertise the server to the masterserver
	uint8  lan_internet;                                  ///< search on the LAN or internet for servers
	char   client_name[NETWORK_NAME_LENGTH];              ///< name of the player (as client)
	char   default_company_pass[NETWORK_PASSWORD_LENGTH]; ///< default password for new companies in encrypted form
	char   connect_to_ip[NETWORK_HOSTNAME_LENGTH];        ///< default for the "Add server" query
	char   network_id[NETWORK_UNIQUE_ID_LENGTH];          ///< semi-unique ID of the client
	bool   autoclean_companies;                           ///< automatically remove companies that are not in use
	uint8  autoclean_unprotected;                         ///< remove passwordless companies after this many months
	uint8  autoclean_protected;                           ///< remove the password from passworded companies after this many months
	uint8  autoclean_novehicles;                          ///< remove companies with no vehicles after this many months
	uint8  max_companies;                                 ///< maximum amount of companies
	uint8  max_clients;                                   ///< maximum amount of clients
	uint8  max_spectators;                                ///< maximum amount of spectators
	Year   restart_game_year;                             ///< year the server restarts
	uint8  min_active_clients;                            ///< minimum amount of active clients to unpause the game
	uint8  server_lang;                                   ///< language of the server
	bool   reload_cfg;                                    ///< reload the config file before restarting
	char   last_host[NETWORK_HOSTNAME_LENGTH];            ///< IP address of the last joined server
	uint16 last_port;                                     ///< port of the last joined server
#else /* ENABLE_NETWORK */
#endif
};

/** Settings related to the creation of games. */
struct GameCreationSettings {
	uint32 generation_seed;                  ///< noise seed for world generation
	Year   starting_year;                    ///< starting date
	uint8  map_x;                            ///< X size of map
	uint8  map_y;                            ///< Y size of map
	byte   land_generator;                   ///< the landscape generator
	byte   oil_refinery_limit;               ///< distance oil refineries allowed from map edge
	byte   snow_line_height;                 ///< a number 0-15 that configured snow line height
	byte   tgen_smoothness;                  ///< how rough is the terrain from 0-3
	byte   tree_placer;                      ///< the tree placer algorithm
	byte   heightmap_rotation;               ///< rotation director for the heightmap
	byte   se_flat_world_height;             ///< land height a flat world gets in SE
	byte   town_name;                        ///< the town name generator used for town names
	byte   landscape;                        ///< the landscape we're currently in
	byte   snow_line;                        ///< the snowline level in this game
	byte   water_borders;                    ///< bitset of the borders that are water
	uint16 custom_town_number;               ///< manually entered number of towns
};

/** Settings related to construction in-game */
struct ConstructionSettings {
	bool   build_on_slopes;                  ///< allow building on slopes
	bool   autoslope;                        ///< allow terraforming under things
	bool   longbridges;                      ///< allow 100 tile long bridges
	bool   signal_side;                      ///< show signals on right side
	bool   extra_dynamite;                   ///< extra dynamite
	bool   road_stop_on_town_road;           ///< allow building of drive-through road stops on town owned roads
	bool   road_stop_on_competitor_road;     ///< allow building of drive-through road stops on roads owned by competitors
	uint8  raw_industry_construction;        ///< type of (raw) industry construction (none, "normal", prospecting)
	bool   freeform_edges;                   ///< allow terraforming the tiles at the map edges
};

/** Settings related to the AI. */
struct AISettings {
	bool   ai_in_multiplayer;                ///< so we allow AIs in multiplayer
	bool   ai_disable_veh_train;             ///< disable types for AI
	bool   ai_disable_veh_roadveh;           ///< disable types for AI
	bool   ai_disable_veh_aircraft;          ///< disable types for AI
	bool   ai_disable_veh_ship;              ///< disable types for AI
	uint32 ai_max_opcode_till_suspend;       ///< max opcode calls till AI will suspend
};

/** Settings related to the old pathfinder. */
struct OPFSettings {
	uint16 pf_maxlength;                     ///< maximum length when searching for a train route for new pathfinder
	byte   pf_maxdepth;                      ///< maximum recursion depth when searching for a train route for new pathfinder
};

/** Settings related to the new pathfinder. */
struct NPFSettings {
	/**
	 * The maximum amount of search nodes a single NPF run should take. This
	 * limit should make sure performance stays at acceptable levels at the cost
	 * of not being perfect anymore.
	 */
	uint32 npf_max_search_nodes;

	uint32 npf_rail_firstred_penalty;        ///< the penalty for when the first signal is red (and it is not an exit or combo signal)
	uint32 npf_rail_firstred_exit_penalty;   ///< the penalty for when the first signal is red (and it is an exit or combo signal)
	uint32 npf_rail_lastred_penalty;         ///< the penalty for when the last signal is red
	uint32 npf_rail_station_penalty;         ///< the penalty for station tiles
	uint32 npf_rail_slope_penalty;           ///< the penalty for sloping upwards
	uint32 npf_rail_curve_penalty;           ///< the penalty for curves
	uint32 npf_rail_depot_reverse_penalty;   ///< the penalty for reversing in depots
	uint32 npf_rail_pbs_cross_penalty;       ///< the penalty for crossing a reserved rail track
	uint32 npf_rail_pbs_signal_back_penalty; ///< the penalty for passing a pbs signal from the backside
	uint32 npf_buoy_penalty;                 ///< the penalty for going over (through) a buoy
	uint32 npf_water_curve_penalty;          ///< the penalty for curves
	uint32 npf_road_curve_penalty;           ///< the penalty for curves
	uint32 npf_crossing_penalty;             ///< the penalty for level crossings
	uint32 npf_road_drive_through_penalty;   ///< the penalty for going through a drive-through road stop
};

/** Settings related to the yet another pathfinder. */
struct YAPFSettings {
	bool   disable_node_optimization;        ///< whether to use exit-dir instead of trackdir in node key
	uint32 max_search_nodes;                 ///< stop path-finding when this number of nodes visited
	bool   ship_use_yapf;                    ///< use YAPF for ships
	bool   road_use_yapf;                    ///< use YAPF for road
	bool   rail_use_yapf;                    ///< use YAPF for rail
	uint32 road_slope_penalty;               ///< penalty for up-hill slope
	uint32 road_curve_penalty;               ///< penalty for curves
	uint32 road_crossing_penalty;            ///< penalty for level crossing
	uint32 road_stop_penalty;                ///< penalty for going through a drive-through road stop
	bool   rail_firstred_twoway_eol;         ///< treat first red two-way signal as dead end
	uint32 rail_firstred_penalty;            ///< penalty for first red signal
	uint32 rail_firstred_exit_penalty;       ///< penalty for first red exit signal
	uint32 rail_lastred_penalty;             ///< penalty for last red signal
	uint32 rail_lastred_exit_penalty;        ///< penalty for last red exit signal
	uint32 rail_station_penalty;             ///< penalty for non-target station tile
	uint32 rail_slope_penalty;               ///< penalty for up-hill slope
	uint32 rail_curve45_penalty;             ///< penalty for curve
	uint32 rail_curve90_penalty;             ///< penalty for 90-deg curve
	uint32 rail_depot_reverse_penalty;       ///< penalty for reversing in the depot
	uint32 rail_crossing_penalty;            ///< penalty for level crossing
	uint32 rail_look_ahead_max_signals;      ///< max. number of signals taken into consideration in look-ahead load balancer
	int32  rail_look_ahead_signal_p0;        ///< constant in polynomial penalty function
	int32  rail_look_ahead_signal_p1;        ///< constant in polynomial penalty function
	int32  rail_look_ahead_signal_p2;        ///< constant in polynomial penalty function
	uint32 rail_pbs_cross_penalty;           ///< penalty for crossing a reserved tile
	uint32 rail_pbs_station_penalty;         ///< penalty for crossing a reserved station tile
	uint32 rail_pbs_signal_back_penalty;     ///< penalty for passing a pbs signal from the backside
	uint32 rail_doubleslip_penalty;          ///< penalty for passing a double slip switch

	uint32 rail_longer_platform_penalty;           ///< penalty for longer  station platform than train
	uint32 rail_longer_platform_per_tile_penalty;  ///< penalty for longer  station platform than train (per tile)
	uint32 rail_shorter_platform_penalty;          ///< penalty for shorter station platform than train
	uint32 rail_shorter_platform_per_tile_penalty; ///< penalty for shorter station platform than train (per tile)
};

/** Settings related to all pathfinders. */
struct PathfinderSettings {
	uint8  pathfinder_for_trains;            ///< the pathfinder to use for trains
	uint8  pathfinder_for_roadvehs;          ///< the pathfinder to use for roadvehicles
	uint8  pathfinder_for_ships;             ///< the pathfinder to use for ships
	bool   new_pathfinding_all;              ///< use the newest pathfinding algorithm for all

	bool   roadveh_queue;                    ///< buggy road vehicle queueing
	bool   forbid_90_deg;                    ///< forbid trains to make 90 deg turns

	byte   wait_oneway_signal;               ///< waitingtime in days before a oneway signal
	byte   wait_twoway_signal;               ///< waitingtime in days before a twoway signal

	bool   reserve_paths;                    ///< always reserve paths regardless of signal type.
	byte   wait_for_pbs_path;                ///< how long to wait for a path reservation.
	byte   path_backoff_interval;            ///< ticks between checks for a free path.

	OPFSettings  opf;                        ///< pathfinder settings for the old pathfinder
	NPFSettings  npf;                        ///< pathfinder settings for the new pathfinder
	YAPFSettings yapf;                       ///< pathfinder settings for the yet another pathfinder
};

/** Settings related to orders. */
struct OrderSettings {
	bool   improved_load;                    ///< improved loading algorithm
	bool   gradual_loading;                  ///< load vehicles gradually
	bool   selectgoods;                      ///< only send the goods to station if a train has been there
	bool   gotodepot;                        ///< allow goto depot in orders
	bool   no_servicing_if_no_breakdowns;    ///< dont send vehicles to depot when breakdowns are disabled
	bool   timetabling;                      ///< whether to allow timetabling
	bool   serviceathelipad;                 ///< service helicopters at helipads automatically (no need to send to depot)
};

/** Settings related to vehicles. */
struct VehicleSettings {
	bool   mammoth_trains;                   ///< allow very long trains
	uint8  train_acceleration_model;         ///< realistic acceleration for trains
	bool   wagon_speed_limits;               ///< enable wagon speed limits
	bool   disable_elrails;                  ///< when true, the elrails are disabled
	UnitID max_trains;                       ///< max trains in game per company
	UnitID max_roadveh;                      ///< max trucks in game per company
	UnitID max_aircraft;                     ///< max planes in game per company
	UnitID max_ships;                        ///< max ships in game per company
	bool   servint_ispercent;                ///< service intervals are in percents
	uint16 servint_trains;                   ///< service interval for trains
	uint16 servint_roadveh;                  ///< service interval for road vehicles
	uint16 servint_aircraft;                 ///< service interval for aircraft
	uint16 servint_ships;                    ///< service interval for ships
	uint8  plane_speed;                      ///< divisor for speed of aircraft
	uint8  freight_trains;                   ///< value to multiply the weight of cargo by
	bool   dynamic_engines;                  ///< enable dynamic allocation of engine data
	bool   never_expire_vehicles;            ///< never expire vehicles
	byte   extend_vehicle_life;              ///< extend vehicle life by this many years
	byte   road_side;                        ///< the side of the road vehicles drive on
};

/** Settings related to the economy. */
struct EconomySettings {
	bool   inflation;                        ///< disable inflation
	bool   bribe;                            ///< enable bribing the local authority
	bool   smooth_economy;                   ///< smooth economy
	bool   allow_shares;                     ///< allow the buying/selling of shares
	byte   dist_local_authority;             ///< distance for town local authority, default 20
	bool   exclusive_rights;                 ///< allow buying exclusive rights
	bool   give_money;                       ///< allow giving other companies money
	bool   mod_road_rebuild;                 ///< roadworks remove unneccesary RoadBits
	bool   multiple_industry_per_town;       ///< allow many industries of the same type per town
	bool   same_industry_close;              ///< allow same type industries to be built close to each other
	uint8  town_growth_rate;                 ///< town growth rate
	uint8  larger_towns;                     ///< the number of cities to build. These start off larger and grow twice as fast
	uint8  initial_city_size;                ///< multiplier for the initial size of the cities compared to towns
	TownLayoutByte town_layout;              ///< select town layout
	bool   allow_town_roads;                 ///< towns are allowed to build roads (always allowed when generating world / in SE)
	bool   station_noise_level;              ///< build new airports when the town noise level is still within accepted limits
	uint16 town_noise_population[3];         ///< population to base decision on noise evaluation (@see town_council_tolerance)
};

/** Settings related to stations. */
struct StationSettings {
	bool   modified_catchment;               ///< different-size catchment areas
	bool   join_stations;                    ///< allow joining of train stations
	bool   nonuniform_stations;              ///< allow nonuniform train stations
	bool   adjacent_stations;                ///< allow stations to be built directly adjacent to other stations
	bool   distant_join_stations;            ///< allow to join non-adjacent stations
	bool   always_small_airport;             ///< always allow small airports
	byte   station_spread;                   ///< amount a station may spread
};

/** All settings together for the game. */
struct GameSettings {
	DifficultySettings   difficulty;         ///< settings related to the difficulty
	GameCreationSettings game_creation;      ///< settings used during the creation of a game (map)
	ConstructionSettings construction;       ///< construction of things in-game
	AISettings           ai;                 ///< what may the AI do?
	class AIConfig      *ai_config[MAX_COMPANIES]; ///< settings per company
	PathfinderSettings   pf;                 ///< settings for all pathfinders
	OrderSettings        order;              ///< settings related to orders
	VehicleSettings      vehicle;            ///< options for vehicles
	EconomySettings      economy;            ///< settings to change the economy
	StationSettings      station;            ///< settings related to station management
	LocaleSettings       locale;             ///< settings related to used currency/unit system in the current game
};

/** All settings that are only important for the local client. */
struct ClientSettings {
	GUISettings          gui;                ///< settings related to the GUI
	NetworkSettings      network;            ///< settings related to the network
};

/** The current settings for this game. */
extern ClientSettings _settings_client;

/** The current settings for this game. */
extern GameSettings _settings_game;

/** The settings values that are used for new games and/or modified in config file. */
extern GameSettings _settings_newgame;

#endif /* SETTINGS_TYPE_H */
