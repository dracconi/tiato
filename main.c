int main() {
  #ifdef TEST
  void test_messaging();
  void test_messaging_mt();
  
  test_messaging();
  test_messaging_mt();
  #endif
  
  return 0;
}
