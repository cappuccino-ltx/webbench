
#include <unistd.h>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <string.h>
#include <cstdlib> 
#include <signal.h>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class SyncConnect
{
private:
    asio::io_context _context;
    tcp::resolver _resolver;
    tcp::resolver::results_type _endpoint;

public:
    SyncConnect(const std::string &ip, const std::string &port)
        : _resolver(_context),
          _endpoint(_resolver.resolve(ip, port))
    {
    }
    void request(const size_t request_num)
    {
        std::atomic<size_t> count{0};
        std::atomic<size_t> failed{0};
        std::mutex mtx;
        std::string req = "GET / HTTP/1.0\r\n\r\n";
        // 创建多线程发起请求，并计数
        // int thread_num = std::thread::hardware_concurrency();
        int thread_num = 1000;
        std::vector<std::thread> threads;
        std::vector<size_t> data(thread_num);
        // 记录一个时间戳
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < thread_num; i++)
        {
            threads.emplace_back([& , i](){
                while(1) {
                {
                    // 线程退出条件
                    std::unique_lock<std::mutex> lock(mtx);
                    if (count >= request_num) {
                        break;
                    }
                    count++;
                }
                // 开始同步请求
                try
                {
                    // 创建一个socket
                    tcp::socket sock(_context);
                    // 发起同步连接
                    asio::connect(sock, _endpoint);
                    // 同步发送数据
                    asio::write(sock, asio::buffer(req));
                    // 同步接收数据
                    std::vector<char> response(1024);
                    int n = sock.read_some(asio::buffer(response.data(), response.size()));
                    // std::cout << "接收到数据" << std::endl << response.data() << std::endl;
                    data[i] += n + req.size();
                }
                catch (const std::exception &e)
                {
                    // std::cerr << "exception : " << e.what() << '\n';
                    failed++;
                }
            } });
        }
        for (int i = 0 ; i < threads.size(); i++) {
            threads[i].join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        // 统计执行时间，毫秒
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds> (end - start);
        // 转化成秒
        size_t time_count = duration.count() / 1000;

        // 总的数据量
        size_t data_count = 0;
        for (auto num : data) {
            data_count += num;
        }

        std::cout << "==========================================" << std::endl;
        std::cout << "请求内容 : " << req << std::endl;
        std::cout << "一共发送请求 : " <<  request_num << std::endl;
        std::cout << "失败次数 : " << failed << std::endl;
        std::cout << "网络吞吐量 : " << (data_count / (time_count == 0 ? 1 : time_count) ) << " byte/s" << std::endl;
        std::cout << "==========================================" << std::endl;
    }
};

class AsyncConnect
{
private:
    asio::io_context _context;
    tcp::resolver _resolver;
    tcp::resolver::results_type _endpoint;
    std::atomic<uint64_t> _count{0};
    std::atomic<uint64_t> _failed{0};
    std::atomic<uint64_t> _data_count{0};
    std::mutex _mtx;
    std::string _req;
    size_t _client;
    size_t _client_reqeust_num;
    size_t _request_num;

public:
    AsyncConnect(const std::string &ip, const std::string &port, size_t client,size_t client_request_num)
        : _resolver(_context),
          _endpoint(_resolver.resolve(ip, port)),
          _req("GET / HTTP/1.0\r\n\r\n"),
          _client(client),
          _client_reqeust_num(client_request_num),
          _request_num(_client * _client_reqeust_num)
    {
    }
    uint64_t getFailed() {
        return _failed;
    }
    uint64_t getCount() {
        return _count;
    }
    uint64_t getDateCount() {
        return _data_count;
    }

    void start()
    {
        auto start = std::chrono::high_resolution_clock::now();

        // 启动多个异步连接
        
        for (size_t i = 0; i < _client; ++i)
        {
            start_async_connect();
        }

        // 运行 io_context
        std::vector<std::thread> threads;
        for (int i = 0; i < _client; ++i)
        {
            threads.emplace_back([this]() { _context.run(); });
        }

        // 等待所有线程完成
        for (auto &t : threads)
        {
            t.join();
        }

        

        // auto end = std::chrono::high_resolution_clock::now();
        // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        // size_t time_count = duration.count() / 1000;

        // std::cout << "==========================================" << std::endl;
        // std::cout << "请求内容 : " << _req << std::endl;
        // std::cout << "一共发送请求 : " << _request_num << std::endl;
        // std::cout << "失败次数 : " << _failed << std::endl;
        // std::cout << "网络吞吐量 : " << (_data_count / (time_count == 0 ? 1 : time_count)) << " byte/s" << std::endl;
        // std::cout << "==========================================" << std::endl;
    }

private:
    void start_async_connect()
    {
        if (_count + _failed >= _request_num) {
            return;  // 如果已经达到请求数量，不再启动新的连接
        }
        auto socket = std::make_shared<tcp::socket>(_context);

        // 异步连接
        asio::async_connect(*socket, _endpoint,
                            [this, socket](boost::system::error_code ec, const tcp::endpoint &) {
                                if (!ec)
                                {
                                    // 连接成功，异步发送请求
                                    async_write(socket);
                                    _count++;
                                }
                                else
                                {
                                    // 连接失败
                                    _failed++;
                                }
                            });
    }

    void async_write(std::shared_ptr<tcp::socket> socket)
    {
        auto buffer = std::make_shared<std::string>(_req);

        // 异步发送数据
        asio::async_write(*socket, asio::buffer(*buffer),
                          [this, socket, buffer](boost::system::error_code ec, std::size_t bytes_transferred) {
                              if (!ec)
                              {
                                  // 发送成功，异步接收数据
                                  async_read(socket);
                              }
                              else
                              {
                                  // 发送失败
                                  _failed++;
                              }
                          });
    }

    void async_read(std::shared_ptr<tcp::socket> socket)
    {
        // auto response = std::make_shared<std::vector<char>>(4096, 0);
        auto response = std::make_shared<std::string>();

        // 异步接收数据
        // socket->async_read_some(asio::buffer(*response),
        asio::async_read(*socket, asio::dynamic_buffer(*response), asio::transfer_at_least(1),
                                [this, socket, response](boost::system::error_code ec, std::size_t bytes_read) {
                                    if (!ec)
                                    {
                                        size_t count = 0;
                                        size_t pos = 0;
                                        while(true) {
                                            pos = strlen(response->data() + count);
                                            if (pos == 0) {
                                                break;
                                            }
                                            count += ++pos;// 算上 \0
                                        }
                                        // 接收成功，统计数据量
                                        _data_count += count - 1 + _req.size(); // 减去最后一个\0

                                        // 关闭连接
                                        socket->close();
                                    }
                                    else
                                    {
                                        // 接收失败
                                        _failed++;
                                    }
                                    start_async_connect();
                                });
    }
};

// 同步请求
int test1(int argc, char ** argv)
{
    if (argc != 4) {
        std::cout << "use " << argv[0] << " + request_num + host + port "<< std::endl;
        std::cout << "用法: " << argv[0] << " <请求数量> <主机> <端口>" << std::endl;
        exit(1);
    }
    int request_num = std::atoi(argv[1]);
    SyncConnect conn(argv[2], argv[3]);
    conn.request(request_num);
    return 0;
}


int test2(int argc, char **argv)
{
    // 默认值
    int c_value = 1;          // -c 的默认值
    int n_value = 10;         // -n 的默认值
    std::string ip;      // -h 的值（必须指定）
    int p_value = 0;          // -p 的值（必须指定）

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg.substr(0, 2) == "-c") {
            // 处理 -c 选项
            if (arg.size() > 2) {
                // 格式: -c5
                c_value = std::atoi(arg.substr(2).c_str());
            } else if (i + 1 < argc) {
                // 格式: -c 5
                c_value = std::atoi(argv[i + 1]);
                ++i;  // 跳过下一个参数
            } else {
                std::cerr << "错误: -c 选项缺少参数\n";
                return 1;
            }
        } else if (arg.substr(0, 2) == "-n") {
            // 处理 -n 选项
            if (arg.size() > 2) {
                // 格式: -n20
                n_value = std::atoi(arg.substr(2).c_str());
            } else if (i + 1 < argc) {
                // 格式: -n 20
                n_value = std::atoi(argv[i + 1]);
                ++i;  // 跳过下一个参数
            } else {
                std::cerr << "错误: -n 选项缺少参数\n";
                return 1;
            }
        } else if (arg.substr(0, 2) == "-h") {
            // 处理 -h 选项
            if (arg.size() > 2) {
                // 格式: -h127.0.0.1
                ip = arg.substr(2);
            } else if (i + 1 < argc) {
                // 格式: -h 127.0.0.1
                ip = argv[i + 1];
                ++i;  // 跳过下一个参数
            } else {
                std::cerr << "错误: -h 选项缺少参数\n";
                return 1;
            }
        } else if (arg.substr(0, 2) == "-p") {
            // 处理 -p 选项
            if (arg.size() > 2) {
                // 格式: -p8080
                p_value = std::atoi(arg.substr(2).c_str());
            } else if (i + 1 < argc) {
                // 格式: -p 8080
                p_value = std::atoi(argv[i + 1]);
                ++i;  // 跳过下一个参数
            } else {
                std::cerr << "错误: -p 选项缺少参数\n";
                return 1;
            }
        } else {
            // 未知选项
            std::cerr << "未知选项: " << arg << std::endl;
            return 1;
        }
    }

    // 检查 -h 和 -p 是否被指定
    if (ip.empty() || p_value == 0) {
        std::cerr << "错误: 必须指定 -h<IP 地址>和 -p<端口号>\n";
        std::cerr << "用法: " << argv[0] << " -h <IP地址> " << std::endl << \
                "-p <端口号> " <<  std::endl << \
                "[-c <请求端数量> 默认 1 ] " <<  std::endl << \
                "[-n <单端请求次数> 默认 10 ] \n";
        return 1;
    }

    // 检查值的范围，
    if (c_value < 1 || c_value > 20000) {
        std::cerr << "错误: client 数量过多，（ 1 ~ 20000 ）\n";
        return 1 ;
    }
    if (n_value < 1 || c_value > 20000) {
        std::cerr << "错误: 大于 0\n";
        return 1 ;
    }
    
    std::string host = ip;
    std::string port = std::to_string(p_value);

    // 计算需要的进程数
    const int threads_per_process = 100;  // 每个进程最多 100 个线程
    int num_processes = (c_value + threads_per_process - 1) / threads_per_process;

    std::vector<pid_t> child_pids;

    // 保存管道
    std::vector<int> pipe_fds(num_processes);

    // 创建子进程
    for (int i = 0; i < num_processes; ++i)
    {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            std::cerr << "创建管道失败\n";
            return 1;
        }
        pid_t pid = fork();
        if (pid == 0)
        {
            
            // 子进程
            close(pipefd[0]);  // 关闭读端
            for (int j = 0; j < i; j++) {
                close(pipe_fds[j]);//关闭父进程关联的其他子进程的管道文件描述符
            }
            
            int clients_per_process = (i == num_processes - 1) ? (c_value - (threads_per_process * i)) : threads_per_process;
            AsyncConnect conn(host, port, clients_per_process, n_value);
            // 开始计时
            auto start = std::chrono::high_resolution_clock::now();
            conn.start();
            // 结束计时
            auto end = std::chrono::high_resolution_clock::now();
            uint64_t start_time = std::chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count();
            uint64_t end_time = std::chrono::duration_cast<std::chrono::milliseconds>(end.time_since_epoch()).count();

            uint64_t data_count = conn.getDateCount();
            uint64_t count = conn.getCount();
            uint64_t failed = conn.getFailed();

            char buffer[128];
            sprintf(buffer,"%lu %lu %lu %lu %lu", start_time, end_time, count,failed,data_count);
            int n = write(pipefd[1],&buffer,sizeof(buffer));
            if (n < 0) {
                abort();
            }
            close(pipefd[1]);
            exit(0);
        }
        else if (pid > 0)
        {
            // 父进程记录子进程 PID
            child_pids.push_back(pid);
            close(pipefd[1]);  // 关闭写端
            pipe_fds[i] = pipefd[0];  // 保存读端
        }
        else
        {
            std::cerr << "创建子进程失败\n";
            return 1;
        }
    }

    // 父进程读取子进程的统计结果
    std::chrono::milliseconds begin = std::chrono::milliseconds::max();
    std::chrono::milliseconds end = std::chrono::milliseconds::min();
    uint64_t total_requests = 0;
    uint64_t failed_requests = 0;
    uint64_t total_data = 0;

    for (int i = 0; i < num_processes; ++i) {
        char buffer[1024];
        ssize_t bytes_read = read(pipe_fds[i], buffer, sizeof(buffer));
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            uint64_t start_time = 0;
            uint64_t end_time = 0;
            uint64_t count = 0;
            uint64_t failed = 0;
            uint64_t data_count = 0;
            sscanf(buffer, "%lu %lu %lu %lu %lu",&start_time, &end_time, &count, &failed, &data_count);
            // 统计时间
            begin = std::min(begin, std::chrono::milliseconds(start_time));
            end = std::max(end, std::chrono::milliseconds(end_time));
            total_requests += count;
            failed_requests += failed;
            total_data += data_count;
        }
        close(pipe_fds[i]);  // 关闭读端
    }

    // 等待所有子进程完成
    for (pid_t pid : child_pids) {
        waitpid(pid, nullptr, 0);
    }

    // 计算总时间
    std::chrono::milliseconds duration(end - begin);

    std::cout << "==========================================" << std::endl;
    std::cout << "发送请求次数 : " << total_requests << std::endl;
    std::cout << "请求失败次数 : " << failed_requests << std::endl;
    std::cout << "平均网络吞吐 : " << (total_data / ((duration.count() / 1000) == 0 ? 1 : (duration.count() / 1000))) << " byte/s" << std::endl;
    std::cout << "==========================================" << std::endl;

    return 0;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    return test2(argc, argv);
}