#include "gtest/gtest.h"
#include <core.h>
#include "sh2core.h"
#include "debug.h"
#include "yabause.h"
#include "memory_for_test.h"
#include "DynarecSh2.h"

namespace {

class ExtsbTest : public ::testing::Test {
 protected:
   DynarecSh2 * pctx_;

  ExtsbTest() {
    initMemory();
    pctx_ = new DynarecSh2();  
    pctx_->SetCurrentContext();
  }

  virtual ~ExtsbTest() {
    delete pctx_;    
  }   

virtual void SetUp() {
  
}

virtual void TearDown() {

}

};

TEST_F(ExtsbTest, normal) {

  pctx_->GetGenRegPtr()[2]=0x00000020; //source
  pctx_->GetGenRegPtr()[1]=0x00FF0036; //source

  // subc r1,r2
  memSetWord( 0x06000000, 0x621e );
  memSetWord( 0x06000002, 0x000b );  // rts
  memSetWord( 0x06000004, 0x0009 );  // nop

  pctx_->SET_PC( 0x06000000 );
  pctx_->Execute();

  EXPECT_EQ( 0x00000036, pctx_->GetGenRegPtr()[2] );
  EXPECT_EQ( 0x00FF0036, pctx_->GetGenRegPtr()[1] );
}

TEST_F(ExtsbTest, normal_T1) {

  pctx_->GetGenRegPtr()[0]=0x00000080; //source

  // subc r1,r2
  memSetWord( 0x06000000, 0x600e );
  memSetWord( 0x06000002, 0x000b );  // rts
  memSetWord( 0x06000004, 0x0009 );  // nop

  pctx_->SET_PC( 0x06000000 );
  pctx_->Execute();

  EXPECT_EQ( 0xffffff80, pctx_->GetGenRegPtr()[0] );
}

}  // namespacegPtr
