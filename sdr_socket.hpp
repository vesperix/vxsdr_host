namespace SDR {
  class socket {
    public:
      socket();
      socket(const socket& orig);
      socket& operator=(const socket& rhs);
      ~socket();
  }
}