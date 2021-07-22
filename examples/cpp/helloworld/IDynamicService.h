
class IDynamicService : public grpc::Service 
{
  public:
    virtual ~IDynamicService() {
      std::cout << "Destroying IDynamicService." << std::endl;
    };
};

