
class TON_ms
{
  private:
    unsigned long tref = 0;
    unsigned long delta = 0;
    bool done = false;
    
  public:
    bool Q = false;         //output true when PT reached.
    unsigned long ET = 0;   //elapsed time. saturates to PT.

    bool IN(bool input, unsigned long PT)
    {
      if (input) {
        if (done) {
          ET = PT;
          Q = true;
        } else {
          delta = millis() - tref;
          Q = (delta > PT);
          if (Q) {
            done = true;
            ET = PT;
          } else {
            ET = delta;
          }
        }
      } else {
        tref = millis();
        ET = 0;
        Q = false;
	      done = false;
      }
      return Q;
    }
};

class Finite_State_Machine
{
  public:
    int nowState = -1;
    int nextState = 0;
    uint32_t timeRef = 0;
    uint32_t timeout = 0;
    bool stateChanged = true;

    int update()
    {
      if (nowState != nextState) {
        stateChanged = true;
        nowState = nextState;
        timeRef = millis();
        timeout = 0;
      } else {
        stateChanged = false;
        timeout = millis() - timeRef;
      }
      return nowState;
    }
};
