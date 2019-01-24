# LinSDN


Written in C, this project simulates a linear SDN where clients can communicate with one another using named pipes.
The clients communicate with the server hosted on the local device using sockets. The sever handles query requests from up
to 7 clients, directing packets to the correct destination and dropping those with an invalid destination IP address. IO multiplexing 
was used to continually poll the connections for new messages and poll any input from the command line.

Specs that were used to design the project were provided by Ehab Elmallah at the University of Alberta.

Included are two test data files.

NOTES:
1) Runnable in a UNIX environment
2) Named pipes must be present in the current working directory in the form fifo-swnum-swnum where 1<=swnum<=7
3) Run server with the command line arguments a3sdn cont num_of_switches port_num where 1<=num_of_switches<=7
4) Run client switches with command line arguments a3sdn sw_num traffic_file sw_num_left_neighbour sw_num_right_neighbour ip-range host_name port_num
        where 7 is the max num of switches and traffic file is either t3.dat or t2.dat and ip-range is at most 1000
5) List command in the server lists the information on switches and list produces a traffic file in the switches with information on how the
        packets located in the traffic file were moved
6) More information on design is included in ReportA3.pdf


** Updates
    * Make more user friendly
