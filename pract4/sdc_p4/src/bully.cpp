#include <chrono>
#include <memory>
#include <queue>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float64.hpp"
#include "std_msgs/msg/int32.hpp"

// posssible values to give to the roleplay global variable 
const std::string RP_LEADER     = "rpL";
const std::string RP_FOLLOWER   = "rpF";
const std::string RP_ELECTION   = "rpE";
const std::string RP_CANDIDATE  = "rpC";
const std::string RP_NEWLEADER  = "rpNL";
const std::string RP_NEWFOLLOW  = "rpNF";

std::string roleplay = "rpNONE";          // global value that determinates the behavior for each role / situation
std::priority_queue<int> candidates_pids; // priority queue that stores all candidate's pids during the election process


using std::placeholders::_1;

class BullyNode : public rclcpp::Node
{
public:
    BullyNode()
    : Node(get_full_name("bully_node_"))
    {
        rclcpp::QoS qos_settings(rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_default));
        qos_settings.keep_last(10);
        qos_settings.reliable();
        qos_settings.durability_volatile();

        // initialize role as None, then get the value introduced (if any)
        this->declare_parameter<std::string>("role", "None");
        std::string role = this->get_parameter("role").as_string();

        // heartbeat pub and sub 
        pub_heartbeat_ = this->create_publisher<std_msgs::msg::Float64>("/sdc/p4/heartbeat", 10);
        sub_heartbeat_ = this->create_subscription<std_msgs::msg::Float64>(
            "/sdc/p4/heartbeat", 10, std::bind(&BullyNode::follower_heartbeat_cb, this, _1));

        // election topic pub and sub (used to start the election process)
        pub_election_ = this->create_publisher<std_msgs::msg::Int32>("/sdc/p4/election", 10);
        sub_election_ = this->create_subscription<std_msgs::msg::Int32>(
            "/sdc/p4/election", 10, std::bind(&BullyNode::heard_election_cb, this, _1));
        
        // candidates' topic pub and sub, used during the election process itself
        pub_candidate_ = this->create_publisher<std_msgs::msg::Int32>("/sdc/p4/candidates_election", 10);
        sub_candidate_ = this->create_subscription<std_msgs::msg::Int32>(
            "/sdc/p4/candidates_election", 10, std::bind(&BullyNode::candidate_sub_cb, this, _1));

        // pub and sub where every new leader is advertised
        pub_newleader_ = this->create_publisher<std_msgs::msg::Int32>("/sdc/p4/new_leader", 10);
        sub_newleader_ = this->create_subscription<std_msgs::msg::Int32>(
            "/sdc/p4/new_leader", 10, std::bind(&BullyNode::become_new_follower_cb, this, _1));

        // new leader by command: start elections to make sure no other previous leader keeps running over this
        if (role == "leader") {
            auto femsg = std_msgs::msg::Int32();
            femsg.data = getpid();
            pub_election_->publish(femsg);

            roleplay = RP_ELECTION; // switch to election role instead of leader (as Gojo said: nah, i'd win)

        // new follower by command: switch roleplay var to its proper value and initialize as it should
        } else if (role == "follower") {
            roleplay = RP_FOLLOWER;
            initialize_as(roleplay);

        // any other value introduced as 'role' parameter produces an error and terminates the execution
        } else {
            RCLCPP_ERROR(this->get_logger(), "'role' parameter non-valid or unset: must be \"leader\" or \"follower\"");
            std::exit(EXIT_FAILURE);
        }
        // initialize_as(roleplay);
    }

private:

    //-- callbacks 
    //-- <{

    //-- leader heartbeat callback (publishes heartbeat and prints info)
    void leader_heartbeat_cb()
    {
        if (roleplay == RP_LEADER) {  // ensure the roleplay is set correctly
            auto hbeat = std_msgs::msg::Float64();
            hbeat.data = this->get_clock()->now().seconds();  // data = EPOCH
            RCLCPP_INFO(this->get_logger(), "[leader] Sending heartbeat");

            pub_heartbeat_->publish(hbeat);
        }
    }

    //-- follower heartbeat callback (resets heart_timer_ and prints info)
    void follower_heartbeat_cb(const std_msgs::msg::Float64::SharedPtr msg) const
    {
        RCLCPP_DEBUG(this->get_logger(), "[debug] FOLLOWER HEARTBEAT CALLBACK TOUCHED BY %s", roleplay.c_str());
        if (roleplay == RP_FOLLOWER || roleplay == RP_NEWFOLLOW) {
            heart_timer_->reset();
            RCLCPP_INFO(this->get_logger(), "[follower] Received heartbeat");
        }
    }

    //-- beginning of election process: called when heart_timer_ ends
    void start_election_cb()
    {
        if (roleplay == RP_FOLLOWER) {
            RCLCPP_WARN(this->get_logger(), "[follower] No heartbeat received in the last 8 seconds!");
            RCLCPP_INFO(this->get_logger(), "[follower] Send message to start election");

            auto emsg = std_msgs::msg::Int32();
            pub_election_->publish(emsg);
            heart_timer_->cancel(); // cancel heartbeat timer so it doesn't restart the elections

            roleplay = RP_ELECTION;
        }
    }
    
    //-- heard the election process: detects the beggining of the elections and changes roleplay to candidate
    void heard_election_cb(const std_msgs::msg::Int32::SharedPtr msg)
    {
        // an old leader can also become candidate (new leader launched by command)
        // if (roleplay == RP_FOLLOWER || roleplay == RP_ELECTION || roleplay == RP_LEADER) {
        if (roleplay == RP_FOLLOWER || roleplay == RP_LEADER) {
            if (!heart_timer_->is_canceled()) {
                heart_timer_->cancel();
            }
        }

        RCLCPP_INFO(this->get_logger(), "[election] Received message for starting the election");
        roleplay = RP_CANDIDATE;

        init_candidate(); // even a follower that sent the message to start the election goes through this instance
        // }
    }

    //-- candidate reception: callback that adds to the candidate's queue every new pid received
    void candidate_sub_cb(const std_msgs::msg::Int32::SharedPtr msg) const
    {
        RCLCPP_DEBUG(this->get_logger(), "[debug] CANDIDATE SUBSCRIPTION CALLBACK TOUCHED BY %s", roleplay.c_str());
        if (roleplay == RP_CANDIDATE) {
            candidates_pids.push(msg->data);  // priority queue with all pid's received
            RCLCPP_INFO(this->get_logger(), "[election] Received candidate leader %i", msg->data);
        }
    }

    //-- finale of the election process: the highest pid becomes the new leader and the lower ones become followers
    void end_of_elections()
    {
        candidate_timer_->cancel(); // cancel the timer for the candidates so the elections do not loop

        auto new_leader_id = candidates_pids.top(); // get the top of the priority queue (highest value)
        if (getpid() == new_leader_id) {  // "if the highest pid is mine, I'm the new leader"
            roleplay = RP_NEWLEADER;

            auto nlmsg = std_msgs::msg::Int32();
            nlmsg.data = new_leader_id;
            pub_newleader_->publish(nlmsg);

            RCLCPP_INFO(this->get_logger(), "[leader] I'm the new leader");
            initialize_as(RP_LEADER);
            
        } else {  // "I'm not the highest pid, I should be follower now, so I'll wait for a new leader announcement"
            roleplay = RP_NEWFOLLOW;
        }

        // empty the priority queue so it doesn't interfiere with the next election process
        while (!candidates_pids.empty()) {
            candidates_pids.pop();
        }
    }

    //-- waiting for a new leader, every RP_NEWFOLLOW becomes finally a true follower when the new leader is announced
    void become_new_follower_cb(const std_msgs::msg::Int32::SharedPtr msg)
    {
        if (roleplay == RP_NEWFOLLOW) {
            roleplay = RP_FOLLOWER; // pre-change the role so the early new heartbeat sent by the new leader can be received
            RCLCPP_INFO(this->get_logger(), "[follower] There is a new leader %i", msg->data);
            initialize_as(RP_FOLLOWER);
        }
    }

    //-- }>
    //-- end of callbacks

    // gets the full node name concatenating 'name_prefix' and 'pid'
    std::string get_full_name(std::string name_prefix)
    {
        std::string name_pid = std::to_string(getpid());
        std::string full_name = name_prefix + name_pid;
        return full_name;
    }

    // creates a heart_timer_ configured and binded depending on the initialization (leader or follower)
    void initialize_as(std::string rp)
    {
        roleplay = rp;  // ensure the roleplay var is set to the new role introduced
        
        if (rp == RP_LEADER) {
            leader_heartbeat_cb();  // publish one heartbeat right when the leader is set and initialized
            heart_timer_ = this->create_wall_timer( // leader: publishes heartbeat every 5s
                std::chrono::seconds(5),
                std::bind(&BullyNode::leader_heartbeat_cb, this));

        } else if (rp == RP_FOLLOWER) {
            heart_timer_ = this->create_wall_timer( // follower: switches to election process after 8s unless heartbeat is received
                std::chrono::seconds(7),
                std::bind(&BullyNode::start_election_cb, this));
        }
    }

    // behaves as a candidate to the elections, sending it's pid and waiting the time it should
    void init_candidate()
    {
        auto cpid = std_msgs::msg::Int32(); // confing message
        cpid.data = getpid();

        RCLCPP_INFO(this->get_logger(), "[election] Sending my PID for leader election: %i", getpid()); // advertises what it's sending
        pub_candidate_->publish(cpid);  // publishes (sends) pid

        candidate_timer_ = this->create_wall_timer( // the elections last 3 seconds
            std::chrono::seconds(2),
            std::bind(&BullyNode::end_of_elections, this));
    }

    // publishers:
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr    pub_heartbeat_;
    rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr      pub_election_;
    rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr      pub_candidate_;
    rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr      pub_newleader_;

    // subscribers:
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr sub_heartbeat_;
    rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr   sub_election_;
    rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr   sub_candidate_;
    rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr   sub_newleader_;

    // timers:
    rclcpp::TimerBase::SharedPtr heart_timer_;
    rclcpp::TimerBase::SharedPtr candidate_timer_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<BullyNode>());
    rclcpp::shutdown();
    return 0;
}
